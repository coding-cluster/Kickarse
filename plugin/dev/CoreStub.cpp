// Kickarse -- placeholder kickarse_core (see top-level CMakeLists.txt, KICKARSE_CORE_STUB).
//
// Minimal, honest-but-simplified implementation of the Envelope/PhaseMap/Engine contract headers
// (src/shared/Envelope.h, src/dsp/Engine.h) so the DPF plugin, its UI, the VST3 validator and the
// hosttest can all be built and exercised before the DSP agent's real engine lands. This is NOT
// meant to sound good or to implement the full spec (modes, multiband, spectral, ring-mod,
// triggering, quick shift, swing precision, etc.) -- only to be RT-safe, allocation-free in
// process(), and to move audio/parameters/state/MIDI/transport through the same shapes the real
// Engine will. Replace by building with the real src/dsp + src/shared/Envelope.cpp instead
// (KICKARSE_CORE_STUB=OFF, the default -- see top-level CMakeLists.txt for the auto-fallback).
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>

#include "shared/Envelope.h"
#include "shared/Bridge.h"
#include "shared/Params.h"
#include "dsp/Engine.h"

namespace kick {
namespace {

float wrap01(float v) noexcept
{
    v -= std::floor(v);
    if (v < 0.f) v += 1.f;
    if (v >= 1.f) v -= 1.f;
    return v;
}

// ---- ASCII (de)serialization helpers, locale-independent via <charconv> -----------------------

void appendFloat(std::string& out, float v)
{
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), v);
    out.append(buf, res.ptr);
    out.push_back(' ');
}

void appendUint(std::string& out, uint32_t v)
{
    char buf[16];
    const auto res = std::to_chars(buf, buf + sizeof(buf), v);
    out.append(buf, res.ptr);
    out.push_back('\n');
}

void skipSpaces(std::string_view& sv) noexcept
{
    std::size_t i = 0;
    while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\n' || sv[i] == '\t' || sv[i] == '\r'))
        ++i;
    sv.remove_prefix(i);
}

bool readTag(std::string_view& sv, std::string_view tag) noexcept
{
    skipSpaces(sv);
    if (sv.substr(0, tag.size()) != tag)
        return false;
    sv.remove_prefix(tag.size());
    return true;
}

bool readFloat(std::string_view& sv, float& out) noexcept
{
    skipSpaces(sv);
    const auto res = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    if (res.ec != std::errc())
        return false;
    sv.remove_prefix(std::size_t(res.ptr - sv.data()));
    return true;
}

bool readInt(std::string_view& sv, int& out) noexcept
{
    skipSpaces(sv);
    const auto res = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    if (res.ec != std::errc())
        return false;
    sv.remove_prefix(std::size_t(res.ptr - sv.data()));
    return true;
}

bool readUint(std::string_view& sv, uint32_t& out) noexcept
{
    skipSpaces(sv);
    const auto res = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    if (res.ec != std::errc())
        return false;
    sv.remove_prefix(std::size_t(res.ptr - sv.data()));
    return true;
}

// MPC-style swing stand-in: within each pair of grid cells the first cell lasts a fraction
// (0.5 + 0.5*swing) of the pair (0.5 = straight, 1.0 = "75%"-per-spec-wording extreme); simplified
// vs. whatever the real DSP/editor settles on, kept here only so PhaseMap round-trips sanely.
float swingWarp(float q, float divisions, float swing) noexcept
{
    if (divisions <= 0.f) return q;
    const float cell        = q * divisions;
    const float pairIndex   = std::floor(cell / 2.f);
    const float withinPair  = cell - pairIndex * 2.f; // [0,2)
    const float firstFrac   = 0.5f + 0.5f * swing;    // [0.5,1.0]
    const float warped = (withinPair < 1.f)
                        ? withinPair * firstFrac
                        : firstFrac + (withinPair - 1.f) * (2.f - firstFrac);
    return (pairIndex * 2.f + warped) / divisions;
}

float swingUnwarp(float p, float divisions, float swing) noexcept
{
    if (divisions <= 0.f) return p;
    const float cell       = p * divisions;
    const float pairIndex  = std::floor(cell / 2.f);
    const float withinPair = cell - pairIndex * 2.f;
    const float firstFrac  = 0.5f + 0.5f * swing;
    const float unwarped = (withinPair < firstFrac)
                          ? withinPair / firstFrac
                          : 1.f + (withinPair - firstFrac) / (2.f - firstFrac);
    return (pairIndex * 2.f + unwarped) / divisions;
}

} // namespace

// -------------------------------------------------------------------------------------------
// Envelope

Envelope::Envelope()
{
    // A simple, recognisable "duck then recover" shape: instant dip, held briefly, linear
    // recovery to unity by 30% of the cycle, then flat.
    count_ = 4;
    nodes_[0] = EnvNode{0.00f, 0.f, 0.f, 0};
    nodes_[1] = EnvNode{0.05f, 0.f, 0.f, 0};
    nodes_[2] = EnvNode{0.30f, 1.f, 0.f, 0};
    nodes_[3] = EnvNode{1.00f, 1.f, 0.f, 0};
}

Envelope Envelope::flat()
{
    Envelope env;
    env.count_ = 2;
    env.nodes_[0] = EnvNode{0.f, 1.f, 0.f, 0};
    env.nodes_[1] = EnvNode{1.f, 1.f, 0.f, 0};
    return env;
}

int Envelope::size() const noexcept
{
    return count_;
}

const EnvNode& Envelope::node(int i) const noexcept
{
    if (i < 0) i = 0;
    if (i >= count_) i = count_ > 0 ? count_ - 1 : 0;
    return nodes_[i];
}

EnvNode& Envelope::node(int i) noexcept
{
    if (i < 0) i = 0;
    if (i >= count_) i = count_ > 0 ? count_ - 1 : 0;
    return nodes_[i];
}

int Envelope::insert(const EnvNode& n)
{
    if (count_ >= kMaxNodes)
        return -1;

    int idx = count_;
    for (int i = 0; i < count_; ++i)
    {
        if (nodes_[i].x > n.x)
        {
            idx = i;
            break;
        }
    }

    for (int i = count_; i > idx; --i)
        nodes_[i] = nodes_[i - 1];

    nodes_[idx] = n;
    ++count_;
    return idx;
}

void Envelope::remove(int i)
{
    if (count_ <= 2 || i <= 0 || i >= count_ - 1)
        return; // endpoints protected, or nothing left to remove

    for (int j = i; j < count_ - 1; ++j)
        nodes_[j] = nodes_[j + 1];

    --count_;
}

void Envelope::normalise()
{
    if (count_ <= 0)
        return;

    std::stable_sort(nodes_, nodes_ + count_,
                      [](const EnvNode& a, const EnvNode& b) { return a.x < b.x; });

    for (int i = 0; i < count_; ++i)
    {
        nodes_[i].x = std::min(1.f, std::max(0.f, nodes_[i].x));
        nodes_[i].y = std::min(1.f, std::max(0.f, nodes_[i].y));
        nodes_[i].tension = std::min(1.f, std::max(-1.f, nodes_[i].tension));
    }

    nodes_[0].x = 0.f;
    nodes_[count_ - 1].x = 1.f;
}

float Envelope::evaluate(float phase) const noexcept
{
    if (count_ <= 0)
        return 1.f;
    if (count_ == 1)
        return nodes_[0].y;

    const float p = wrap01(phase);

    int i = 0;
    while (i < count_ - 1 && nodes_[i + 1].x < p)
        ++i;
    if (i >= count_ - 1)
        return nodes_[count_ - 1].y;

    const EnvNode& a = nodes_[i];
    const EnvNode& b = nodes_[i + 1];
    if (b.x <= a.x) // vertical step
        return b.y;

    const float u = shape((p - a.x) / (b.x - a.x), a.tension);
    return a.y + (b.y - a.y) * u;
}

void Envelope::render(float* out, int n) const noexcept
{
    if (n <= 0)
        return;
    for (int i = 0; i < n; ++i)
        out[i] = evaluate(float(i) / float(n));
}

float Envelope::shape(float u, float tension) noexcept
{
    u = std::min(1.f, std::max(0.f, u));
    const float t = std::min(1.f, std::max(-1.f, tension));
    if (t == 0.f)
        return u;

    // Simplified monotonic power curve: shape(u,0) == u, shape(0,*) == 0, shape(1,*) == 1.
    // Not an exact mirror of shape(u,-t) about the curve's midpoint -- good enough for a stub,
    // the real editor's curve math is the DSP/design agents' call.
    const float k = std::pow(2.0f, -t * 4.0f);
    return std::pow(u, k);
}

std::string Envelope::serialize() const
{
    std::string out;
    out += "kkenv1 ";
    appendUint(out, uint32_t(count_));

    for (int i = 0; i < count_; ++i)
    {
        appendFloat(out, nodes_[i].x);
        appendFloat(out, nodes_[i].y);
        appendFloat(out, nodes_[i].tension);
        appendUint(out, nodes_[i].flags);
    }
    return out;
}

bool Envelope::deserialize(std::string_view text)
{
    std::string_view sv = text;
    if (!readTag(sv, "kkenv1"))
        return false;

    int count = 0;
    if (!readInt(sv, count) || count < 2 || count > kMaxNodes)
        return false;

    EnvNode tmp[kMaxNodes];
    for (int i = 0; i < count; ++i)
    {
        float x, y, tension;
        uint32_t flags;
        if (!readFloat(sv, x) || !readFloat(sv, y) || !readFloat(sv, tension) || !readUint(sv, flags))
            return false;
        tmp[i] = EnvNode{x, y, tension, flags};
    }

    count_ = count;
    std::memcpy(nodes_, tmp, sizeof(EnvNode) * std::size_t(count));
    normalise();
    return true;
}

Envelope Envelope::fromSamples(const float* y, int n, float tolerance)
{
    (void)tolerance; // stub: coarse fixed decimation, not a real tolerance-driven fit

    Envelope env;
    env.count_ = 0;

    if (n <= 0 || y == nullptr)
        return Envelope();

    const int step = std::max(1, n / (kMaxNodes - 1));
    for (int i = 0; i < n && env.count_ < kMaxNodes - 1; i += step)
    {
        EnvNode node;
        node.x = float(i) / float(std::max(1, n - 1));
        node.y = std::min(1.f, std::max(0.f, y[i]));
        env.nodes_[env.count_++] = node;
    }

    EnvNode last;
    last.x = 1.f;
    last.y = std::min(1.f, std::max(0.f, y[n - 1]));
    env.nodes_[env.count_++] = last;

    env.normalise();
    return env;
}

// -------------------------------------------------------------------------------------------
// PhaseMap

float PhaseMap::toNode(float timelinePhase) const noexcept
{
    const float q = wrap01(timelinePhase - rotate01);
    return wrap01(swingUnwarp(q, divisions, swing));
}

float PhaseMap::toTimeline(float nodePhase) const noexcept
{
    const float p = swingWarp(wrap01(nodePhase), divisions, swing);
    return wrap01(p + rotate01);
}

// -------------------------------------------------------------------------------------------
// Engine

struct Engine::Impl
{
    std::atomic<float> params[kParamCount];
    Bridge bridge_;

    double   sampleRate   = 44100.0;
    uint32_t maxBlockSize = 512;
    double   phaseAccum   = 0.0;

    // Small ring per band so setEnvelope() (non-RT caller) never blocks or frees memory the
    // audio thread might still be reading, without needing real epoch-based reclamation -- good
    // enough for a stub where envelope edits happen at UI rates, not audio rates.
    static constexpr int kEnvSlots = 4;
    Envelope envSlots[2][kEnvSlots];
    std::atomic<int> envActive[2];
    std::atomic<int> envNextFree[2];

    Impl()
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            params[i].store(kParams[i].def, std::memory_order_relaxed);
        for (int b = 0; b < 2; ++b)
        {
            envActive[b].store(0, std::memory_order_relaxed);
            envNextFree[b].store(1, std::memory_order_relaxed);
        }
    }
};

Engine::Engine() : impl_(new Impl()) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, uint32_t maxBlockSize)
{
    impl_->sampleRate   = sampleRate > 0.0 ? sampleRate : 44100.0;
    impl_->maxBlockSize = maxBlockSize > 0 ? maxBlockSize : 512;
}

void Engine::reset()
{
    impl_->phaseAccum = 0.0;
}

void Engine::setParameter(uint32_t id, float value) noexcept
{
    if (id >= kParamCount)
        return;
    const ParamInfo& info = kParams[id];
    value = std::min(info.max, std::max(info.min, value));
    impl_->params[id].store(value, std::memory_order_relaxed);
}

float Engine::getParameter(uint32_t id) const noexcept
{
    if (id >= kParamCount)
        return 0.f;
    return impl_->params[id].load(std::memory_order_relaxed);
}

void Engine::setEnvelope(int band, const Envelope& env)
{
    if (band < 0 || band > 1)
        return;

    const int slot = impl_->envNextFree[band].load(std::memory_order_relaxed);
    impl_->envSlots[band][slot] = env;
    impl_->envActive[band].store(slot, std::memory_order_release);
    impl_->envNextFree[band].store((slot + 1) % Impl::kEnvSlots, std::memory_order_relaxed);
}

void Engine::process(const float* const* mainIn, const float* const* scIn, float* const* out,
                      uint32_t frames, const TransportInfo& transport,
                      const NoteEvent* notes, uint32_t numNotes) noexcept
{
    (void)notes;
    (void)numNotes; // stub does not implement Sync/MIDI/Audio triggering, only free-running time

    Impl& impl = *impl_;

    const float bypass    = impl.params[kParamBypass].load(std::memory_order_relaxed);
    const float depthPct  = impl.params[kParamDepth].load(std::memory_order_relaxed);
    const float outGainDb = impl.params[kParamOutGain].load(std::memory_order_relaxed);
    const float lengthMs  = impl.params[kParamLengthMs].load(std::memory_order_relaxed);

    const float  depth        = depthPct / 100.f;
    const float  outGain      = std::pow(10.f, outGainDb / 20.f);
    const double cycleSeconds = std::max(0.001, double(lengthMs) / 1000.0);
    const double sr           = impl.sampleRate > 0.0 ? impl.sampleRate : 44100.0;

    const int activeA = impl.envActive[0].load(std::memory_order_acquire);
    const Envelope& envA = impl.envSlots[0][activeA];

    Bridge& bridge = impl.bridge_;
    bridge.bpm.store(float(transport.bpm), std::memory_order_relaxed);
    bridge.hostPlaying.store(transport.playing ? 1 : 0, std::memory_order_relaxed);
    bridge.cycleSeconds.store(float(cycleSeconds), std::memory_order_relaxed);

    float lastY = 1.f;
    float lastPhaseF = 0.f;

    for (uint32_t i = 0; i < frames; ++i)
    {
        const double phase = std::fmod(impl.phaseAccum + double(i) / sr / cycleSeconds, 1.0);
        const float y = envA.evaluate(float(phase));
        const float gain = (bypass > 0.5f) ? 1.f : (1.f - depth * (1.f - y)) * outGain;

        const float m0 = mainIn[0] != nullptr ? mainIn[0][i] : 0.f;
        const float m1 = mainIn[1] != nullptr ? mainIn[1][i] : 0.f;
        out[0][i] = m0 * gain;
        out[1][i] = m1 * gain;

        lastY = y;
        lastPhaseF = float(phase);

        const int bin = std::min(Bridge::kWaveBins - 1, std::max(0, int(phase * Bridge::kWaveBins)));
        const float scPeak = std::max(std::fabs(scIn[0] != nullptr ? scIn[0][i] : 0.f),
                                       std::fabs(scIn[1] != nullptr ? scIn[1][i] : 0.f));
        bridge.mainWave[bin].store(std::max(std::fabs(m0), std::fabs(m1)), std::memory_order_relaxed);
        bridge.extWave[bin].store(scPeak, std::memory_order_relaxed);
        bridge.outWave[bin].store(std::max(std::fabs(out[0][i]), std::fabs(out[1][i])),
                                   std::memory_order_relaxed);
    }

    impl.phaseAccum = std::fmod(impl.phaseAccum + double(frames) / sr / cycleSeconds, 1.0);

    bridge.phase.store(lastPhaseF, std::memory_order_relaxed);
    bridge.valueA.store(lastY, std::memory_order_relaxed);
    bridge.valueB.store(lastY, std::memory_order_relaxed);
    bridge.gainReductionDb.store(20.f * std::log10(std::max(1.0e-6f, 1.f - depth * (1.f - lastY))),
                                  std::memory_order_relaxed);
}

Bridge& Engine::bridge() noexcept
{
    return impl_->bridge_;
}

const Bridge& Engine::bridge() const noexcept
{
    return impl_->bridge_;
}

} // namespace kick
