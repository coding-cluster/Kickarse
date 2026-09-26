// Kickarse UI — per-frame copy of the Bridge (DSP → UI data) plus display ballistics. Filled by
// View::poll() from the real Bridge, or by DemoFeed when KICKARSE_UI_DEMO is set (design review
// without audio).
#pragma once

#include <cstdint>

#include "shared/Bridge.h"

namespace kick { namespace ui {

struct Live {
    static constexpr int kBins = Bridge::kWaveBins;

    float    phase = 0.f, valueA = 1.f, valueB = 1.f, grDb = 0.f;
    std::uint32_t triggerCount = 0;
    bool     active = true, playing = false;
    float    bpm = 120.f, cycleSeconds = 0.5f;
    int      timeSigNum = 4, timeSigDen = 4;
    float    scLevelDb = -120.f, outPeakDb = -120.f;
    int      lastNote = -1, lastVelocity = 0;
    std::uint32_t noteCount = 0;

    // waveforms (display-smoothed), spectral bands
    float mainWave[kBins] = {}, extWave[kBins] = {}, outWave[kBins] = {}, ringWave[kBins] = {};
    float specCutDb[kSpecBands] = {}, specScDb[kSpecBands] = {}, specHz[kSpecBands] = {};

    // capture handshake
    int   recState = 0;
    float recProgress = 0.f;

    // UI-side flashes (1 → 0), driven by triggerCount / noteCount changes
    float trigFlash = 0.f, noteFlash = 0.f;
    float scPeakHoldDb = -120.f;
};

}} // namespace kick::ui
