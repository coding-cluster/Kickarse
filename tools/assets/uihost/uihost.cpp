// Kickarse — minimal VST2 editor host used only to capture UI snapshots of the real plugin
// (the standalone needs an audio device with 4 inputs). It loads Kickarse-vst2.dll, opens the
// editor in a Win32 window, feeds a steady stream of audio blocks (a synthetic kick + bass, so the
// Bridge has live data) and pumps effEditIdle. The plugin's own debug hook
// (KICKARSE_UI_SNAPSHOT / KICKARSE_UI_SNAPSHOT_EXIT) writes the image and ends the process.
//
// usage: kickarse_uihost <path to Kickarse-vst2.dll> [seconds]
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "xaymar-vst2/vst.h"

namespace {

constexpr int kOpEditGetRect = 0x0D, kOpEditOpen = 0x0E, kOpEditClose = 0x0F, kOpEditIdle = 0x13;
constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;
HWND g_wnd = nullptr;

struct ERect { int16_t top, left, bottom, right; };

struct TimeInfo {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} g_time {};
double g_samples = 0.0;

intptr_t VST_FUNCTION_INTERFACE hostCb(vst_effect*, VST_HOST_OPCODE opcode, int32_t index, int64_t value, void*, float)
{
    switch (int(opcode)) {
    case 0x01: return 2400;                         // audioMasterVersion
    case 0x10: return intptr_t(kSampleRate);        // audioMasterGetSampleRate
    case 0x11: return kBlock;                       // audioMasterGetBlockSize
    case 0x07:                                      // audioMasterGetTime
        g_time.sampleRate = kSampleRate;
        g_time.samplePos = g_samples;
        g_time.tempo = 124.0;
        g_time.ppqPos = g_samples / kSampleRate * 124.0 / 60.0;
        g_time.timeSigNumerator = 4;
        g_time.timeSigDenominator = 4;
        g_time.flags = (1 << 1) | (1 << 9) | (1 << 10) | (1 << 13);
        return reinterpret_cast<intptr_t>(&g_time);
    case 0x0F:                                      // audioMasterSizeWindow
        if (g_wnd != nullptr) {
            RECT r {0, 0, LONG(index), LONG(value)};
            AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
            SetWindowPos(g_wnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        return 1;
    default: return 0;
    }
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: kickarse_uihost <Kickarse-vst2.dll> [seconds]\n"); return 2; }
    const double seconds = argc > 2 ? std::atof(argv[2]) : 20.0;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HMODULE lib = LoadLibraryA(argv[1]);
    if (lib == nullptr) { std::fprintf(stderr, "LoadLibrary failed\n"); return 1; }
    using MainFn = vst_effect* (VST_FUNCTION_INTERFACE*)(vst_host_callback);
    auto mainFn = reinterpret_cast<MainFn>(GetProcAddress(lib, "VSTPluginMain"));
    vst_effect* fx = mainFn ? mainFn(hostCb) : nullptr;
    if (fx == nullptr) { std::fprintf(stderr, "no effect\n"); return 1; }
    fx->control(fx, VST_EFFECT_OPCODE_CREATE, 0, 0, nullptr, 0.f);
    fx->control(fx, VST_EFFECT_OPCODE_SETSAMPLERATE, 0, 0, nullptr, float(kSampleRate));
    fx->control(fx, VST_EFFECT_OPCODE_SETBLOCKSIZE, 0, kBlock, nullptr, 0.f);
    fx->control(fx, VST_EFFECT_OPCODE_SUSPEND, 0, 1, nullptr, 0.f);
    // Emulate a DAW that announces its content scale (Bitwig/Reaper style 'PreS'/'AeCs' vendor call).
    if (const char* s = std::getenv("KICKARSE_HOST_SCALE"))
        fx->control(fx, VST_EFFECT_OPCODE(50), int32_t(0x50726553) /*PreS*/, int64_t(0x41654373) /*AeCs*/, nullptr,
                    float(std::atof(s)));

    WNDCLASSW wc {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"KickarseUiHost";
    RegisterClassW(&wc);
    ERect* rect = nullptr;
    fx->control(fx, VST_EFFECT_OPCODE(kOpEditGetRect), 0, 0, &rect, 0.f);
    int w = rect ? rect->right - rect->left : 1080, h = rect ? rect->bottom - rect->top : 660;
    RECT wr {0, 0, w, h};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
    g_wnd = CreateWindowW(L"KickarseUiHost", L"Kickarse UI host", (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME) | WS_VISIBLE, 0, 0,
                          wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, wc.hInstance, nullptr);
    fx->control(fx, VST_EFFECT_OPCODE(kOpEditOpen), 0, 0, g_wnd, 0.f);
    fx->control(fx, VST_EFFECT_OPCODE(kOpEditGetRect), 0, 0, &rect, 0.f);
    if (rect) {
        RECT r2 {0, 0, rect->right - rect->left, rect->bottom - rect->top};
        AdjustWindowRect(&r2, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
        SetWindowPos(g_wnd, nullptr, 0, 0, r2.right - r2.left, r2.bottom - r2.top, SWP_NOMOVE | SWP_NOZORDER);
    }

    std::vector<float> in[4], out[2];
    for (auto& v : in) v.assign(kBlock, 0.f);
    for (auto& v : out) v.assign(kBlock, 0.f);
    float* ins[4] = {in[0].data(), in[1].data(), in[2].data(), in[3].data()};
    float* outs[2] = {out[0].data(), out[1].data()};
    const DWORD start = GetTickCount();
    MSG msg;
    bool running = true;
    while (running && (GetTickCount() - start) < DWORD(seconds * 1000.0)) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // ~one block per 11 ms keeps the playhead near real time
        for (int i = 0; i < kBlock; ++i) {
            const double t = (g_samples + i) / kSampleRate, beat = 60.0 / 124.0, tb = std::fmod(t, beat);
            const float kick = float(0.9 * std::exp(-tb / 0.07) * std::sin(2.0 * 3.14159265 * (55.0 + 90.0 * std::exp(-tb / 0.02)) * tb));
            const float bass = float(0.5 * std::sin(2.0 * 3.14159265 * 55.0 * t));
            in[0][size_t(i)] = in[1][size_t(i)] = bass;
            in[2][size_t(i)] = in[3][size_t(i)] = kick;
        }
        fx->process_float(fx, ins, outs, kBlock);
        g_samples += kBlock;
        fx->control(fx, VST_EFFECT_OPCODE(kOpEditIdle), 0, 0, nullptr, 0.f);
        Sleep(11);
    }
    fx->control(fx, VST_EFFECT_OPCODE(kOpEditClose), 0, 0, nullptr, 0.f);
    fx->control(fx, VST_EFFECT_OPCODE_DESTROY, 0, 0, nullptr, 0.f);
    DestroyWindow(g_wnd);
    return 0;
}
