// Kickarse — deterministic test signals, a stereo buffer type, an engine driver and a WAV writer.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "dsp/Engine.h"

namespace kt {

constexpr double kPi = 3.14159265358979323846;

struct Rng {
    uint32_t s = 0x12345678u;
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uniform() { return float(next() >> 8) * (1.f / 16777216.f); } // [0,1)
    float bipolar() { return uniform() * 2.f - 1.f; }
    int   range(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }
};

struct Stereo {
    std::vector<float> l, r;
    Stereo() = default;
    explicit Stereo(size_t n) : l(n, 0.f), r(n, 0.f) {}
    size_t size() const { return l.size(); }
    void   addMono(const std::vector<float>& m, float gain = 1.f)
    {
        for (size_t i = 0; i < std::min(size(), m.size()); ++i) {
            l[i] += gain * m[i];
            r[i] += gain * m[i];
        }
    }
};

// Synthetic kick: pitch sweep 170 → 48 Hz, 100 ms amplitude decay, optional 1.5 ms noise click.
inline void addKick(std::vector<float>& buf, double sr, size_t onset, float gain, bool click, uint32_t seed = 1)
{
    const size_t len  = size_t(0.35 * sr);
    const size_t fade = size_t(0.02 * sr);
    Rng rng;
    rng.s = seed * 2654435761u + 1u;
    for (size_t i = 0; i < len && onset + i < buf.size(); ++i) {
        const double t     = double(i) / sr;
        const double phase = 2.0 * kPi * (48.0 * t + 122.0 * 0.03 * (1.0 - std::exp(-t / 0.03)));
        double amp = std::exp(-t / 0.1);
        if (i + fade > len)
            amp *= double(len - i) / double(fade);
        double v = amp * std::sin(phase);
        if (click && t < 0.0015)
            v += 0.3 * rng.bipolar() * (1.0 - t / 0.0015);
        buf[onset + i] += float(gain * v);
    }
}

// Band-limited saw-like bass note (6 harmonics).
inline void addBass(std::vector<float>& buf, double sr, size_t start, size_t len, double hz, float gain)
{
    const size_t ramp = size_t(0.005 * sr);
    for (size_t i = 0; i < len && start + i < buf.size(); ++i) {
        const double t = double(i) / sr;
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin(2.0 * kPi * hz * h * t) / h;
        double env = 1.0;
        if (i < ramp)
            env = double(i) / double(ramp);
        if (i + ramp > len)
            env = double(len - i) / double(ramp);
        buf[start + i] += float(gain * 0.55 * v * env);
    }
}

inline void addSine(std::vector<float>& buf, double sr, size_t start, size_t len, double hz, float gain)
{
    for (size_t i = 0; i < len && start + i < buf.size(); ++i)
        buf[start + i] += float(gain * std::sin(2.0 * kPi * hz * double(i) / sr));
}

// Short high-passed noise burst (hi-hat).
inline void addHat(std::vector<float>& buf, double sr, size_t onset, float gain, uint32_t seed)
{
    Rng rng;
    rng.s = seed * 747796405u + 7u;
    const size_t len = size_t(0.03 * sr);
    float prev = 0.f;
    for (size_t i = 0; i < len && onset + i < buf.size(); ++i) {
        const float n = rng.bipolar();
        const float env = std::exp(-float(i) / float(0.008 * sr));
        buf[onset + i] += gain * 0.5f * (n - prev) * env;
        prev = n;
    }
}

inline float peakAbs(const std::vector<float>& v, size_t a = 0, size_t b = SIZE_MAX)
{
    float p = 0.f;
    for (size_t i = a; i < std::min(b, v.size()); ++i)
        p = std::max(p, std::fabs(v[i]));
    return p;
}

// Amplitude of a sinusoid at hz within [a,b) (single-bin DFT, Hann window, amplitude-corrected).
inline double toneAmplitude(const std::vector<float>& v, size_t a, size_t b, double hz, double sr)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    const size_t n = b - a;
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(n));
        const double ph = 2.0 * kPi * hz * double(i) / sr;
        re += w * v[a + i] * std::cos(ph);
        im -= w * v[a + i] * std::sin(ph);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

inline double toDb(double g)
{
    return 20.0 * std::log10(std::max(g, 1e-12));
}

// Drives an Engine over whole buffers with a configurable block size and transport.
struct Driver {
    kick::Engine& engine;
    double        sr;
    bool          transportValid   = false;
    bool          transportPlaying = false;
    double        bpm              = 120.0;
    double        ppqStart         = 0.0;
    int           timeSigNum       = 4;
    int           timeSigDen       = 4;

    struct Note {
        size_t  frame;
        uint8_t note;
        uint8_t velocity;
    };

    // Processes main/sc into out. blockFn(i) returns the size of the i-th block (1..).
    template <typename BlockFn>
    void run(const Stereo& main, const Stereo* sc, Stereo& out, BlockFn blockFn,
             const std::vector<Note>& notes = {}, size_t startFrame = 0)
    {
        const size_t n = main.size();
        out = Stereo(n);
        std::vector<float> zeros(n, 0.f);
        size_t pos = 0, noteIdx = 0;
        int    block = 0;
        std::vector<kick::NoteEvent> ev;
        while (pos < n) {
            const size_t len = std::min(n - pos, size_t(std::max(1, blockFn(block++))));
            const float* in[2]  = {main.l.data() + pos, main.r.data() + pos};
            const float* sci[2] = {sc ? sc->l.data() + pos : zeros.data() + pos,
                                   sc ? sc->r.data() + pos : zeros.data() + pos};
            float* o[2] = {out.l.data() + pos, out.r.data() + pos};
            kick::TransportInfo ti;
            ti.valid   = transportValid;
            ti.playing = transportPlaying;
            ti.bpm     = bpm;
            ti.ppq     = ppqStart + double(startFrame + pos) * bpm / 60.0 / sr;
            ti.timeSigNum = timeSigNum;
            ti.timeSigDen = timeSigDen;
            ev.clear();
            while (noteIdx < notes.size() && notes[noteIdx].frame < pos + len) {
                const Note& nt = notes[noteIdx++];
                kick::NoteEvent e;
                e.frame    = uint32_t(nt.frame - pos);
                e.note     = nt.note;
                e.velocity = nt.velocity;
                e.on       = nt.velocity > 0;
                ev.push_back(e);
            }
            engine.process(in, sci, o, uint32_t(len), ti, ev.data(), uint32_t(ev.size()));
            pos += len;
        }
    }

    void run(const Stereo& main, const Stereo* sc, Stereo& out, int blockSize = 512,
             const std::vector<Note>& notes = {})
    {
        run(main, sc, out, [blockSize](int) { return blockSize; }, notes);
    }
};

// 32-bit float stereo WAV.
inline bool writeWav(const std::string& path, const Stereo& s, int sampleRate)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const uint32_t frames = uint32_t(s.size());
    const uint32_t dataBytes = frames * 2u * 4u;
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36u + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(3); // IEEE float
    u16(2);
    u32(uint32_t(sampleRate));
    u32(uint32_t(sampleRate) * 8u);
    u16(8);
    u16(32);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        const float fr[2] = {s.l[i], s.r[i]};
        std::fwrite(fr, 4, 2, f);
    }
    std::fclose(f);
    return true;
}

} // namespace kt
