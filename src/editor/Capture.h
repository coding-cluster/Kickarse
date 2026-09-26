// Kickarse — audio → envelope conversion for the capture feature (Bridge::recBuf → Envelope).
//
// Pipeline (all pure, UI thread):
//   1. |recBuf| per timeline bin (raw per-bin peaks, they ripple at the kick's frequency);
//   2. peak hold over [−holdMs, +lookaheadMs] (bridges the gaps between the waveform's half-cycles;
//      the look-ahead makes the duck full at the attack's onset, the recovery lags by ≤ holdMs);
//   3. instant-attack / exponential-release follower with releaseMs (smooths the staircase),
//      run cyclically so the tail of the cycle feeds its start;
//   4. normalise to the loudest bin; below silenceThreshold the capture is rejected;
//   5. invert: y = 1 − level, so a kick becomes a duck (low at the hit, recovering after);
//   6. map timeline → node space through the PhaseMap (so rotate/swing play it back in place);
//   7. Envelope::fromSamples with `tolerance`, refitted with a growing tolerance until it has at
//      most maxNodes nodes (then greedily simplified if it still doesn't fit).
// Bins are samples at timeline i / bins (the render() convention), the last bin is held to the
// cycle end, so a kick at phase 0 gives an exact full duck at x = 0.
#pragma once

#include <vector>

#include "EditorTypes.h"

namespace kick::editor {

struct CaptureOptions {
    float cycleSeconds     = 0.5f;   // Bridge::cycleSeconds at capture time
    float holdMs           = 15.f;   // >= half a period of the lowest expected kick fundamental
    float lookaheadMs      = 3.f;    // the hold also looks this far ahead (not across the cycle end), so
                                     // the duck is already full at the onset of a kick's attack
    float releaseMs        = 8.f;    // 5–10 ms keeps the recovery tight
    float tolerance        = 0.01f;  // fit tolerance (max |error| in y) before node limiting
    int   maxNodes         = 32;     // 2 … Envelope::kMaxNodes
    float silenceThreshold = 1e-4f;  // linear peak below which the capture is treated as silence
};

struct CaptureStats {
    float peak          = 0.f;  // loudest raw bin (linear)
    float usedTolerance = 0.f;  // tolerance of the final fit (>= options.tolerance)
    float maxError      = 0.f;  // max |envelope − contour| over the node-space samples
    int   nodes         = 0;
};

// Steps 1–5: the inverted, normalised contour per timeline bin (same length as recBuf).
// Returns an empty vector for null/empty input or silence.
std::vector<float> captureContour(const float* recBuf, int bins, const CaptureOptions& opt);

// The whole pipeline. Returns false (and leaves `out` untouched) for null/empty input or silence.
bool envelopeFromCapture(const float* recBuf, int bins, const PhaseMap& map, const CaptureOptions& opt,
                         Envelope& out, CaptureStats* stats = nullptr);

} // namespace kick::editor
