// Kickarse UI — synthetic Bridge data for design review without audio (KICKARSE_UI_DEMO=1): a
// 124 bpm kick with an off-beat hat, a sustained bass, spectral bands and trigger/MIDI activity,
// computed from the current parameters and envelope exactly like the HTML prototype's fake engine.
// Never used in normal operation.
#pragma once

#include <cstdint>

#include "Live.h"

namespace kick { namespace ui {

class Model;

class DemoFeed {
public:
    // Advance by dt seconds and write into live. freezePhase >= 0 pins the playhead.
    void step(const Model& m, Live& live, double dt, float freezePhase);

private:
    float kickAt(float t) const;
    double simT_ = 0.0;
    long long lastBeat_ = -1;
    double sinceTrig_ = 1e9;
    float prevPhase_ = 0.f;
    std::uint32_t trig_ = 0, notes_ = 0;
    float specSmooth_[kSpecBands] = {};
    float specCut_[kSpecBands] = {};
    float scPeak_ = -120.f;
};

}} // namespace kick::ui
