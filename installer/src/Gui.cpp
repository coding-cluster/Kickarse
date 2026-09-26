#include "Gui.h"
#include "InstallLogic.h"
#include "resource.h"

#include <windows.h>
#include <commctrl.h>
#include <prsht.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>

#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

extern HINSTANCE g_hInst;

namespace kick {

namespace {

constexpr UINT WM_KK_PROGRESS = WM_APP + 1;
constexpr UINT WM_KK_DONE     = WM_APP + 2;

struct WizardContext {
    ParsedArgs initialArgs;

    bool vst3 = true, vst2 = true, clap = true;
    std::wstring vst2Dir;

    HWND hSheet = nullptr;
    HWND hInstallPage = nullptr;

    bool installStarted = false;
    bool installFinished = false;
    bool installOk = false;
    std::wstring installError;
    InstalledPaths resultPaths;
};

// Only one wizard runs per process, so a single static pointer is enough to bridge the
// PropSheetProc callback (which gets no lParam of our own) to the context.
WizardContext* g_ctx = nullptr;

std::wstring QuoteArg(const std::wstring& s) { return L"\"" + s + L"\""; }

InstallOptions BuildOptionsFromCtx(const WizardContext& ctx) {
    InstallOptions opts;
    opts.installVst3 = ctx.vst3;
    opts.installVst2 = ctx.vst2;
    opts.installClap = ctx.clap;
    opts.vst2Dir = ctx.vst2Dir;
    opts.vst3Dir = ctx.initialArgs.vst3Dir;
    opts.clapDir = ctx.initialArgs.clapDir;
    opts.noReg = ctx.initialArgs.noReg;
    opts.manifestPathOverride = ctx.initialArgs.manifestPath;
    return opts;
}

std::wstring BuildElevatedGuiArgs(const WizardContext& ctx) {
    std::wstring a = L"/ELEVATEDGUI /COMPONENTS=";
    std::vector<std::wstring> comps;
    if (ctx.vst3) comps.push_back(L"vst3");
    if (ctx.vst2) comps.push_back(L"vst2");
    if (ctx.clap) comps.push_back(L"clap");
    for (size_t i = 0; i < comps.size(); ++i) {
        if (i) a += L",";
        a += comps[i];
    }
    if (ctx.vst2) a += L" /VST2DIR=" + QuoteArg(ctx.vst2Dir);
    if (!ctx.initialArgs.vst3Dir.empty()) a += L" /VST3DIR=" + QuoteArg(ctx.initialArgs.vst3Dir);
    if (!ctx.initialArgs.clapDir.empty()) a += L" /CLAPDIR=" + QuoteArg(ctx.initialArgs.clapDir);
    if (ctx.initialArgs.noReg) a += L" /NOREG";
    if (!ctx.initialArgs.manifestPath.empty()) a += L" /MANIFEST=" + QuoteArg(ctx.initialArgs.manifestPath);
    return a;
}

std::wstring BuildFinishText(const WizardContext& ctx) {
    std::wstring t;
    if (ctx.installOk) {
        t = L"Kickarse was installed successfully.\r\n\r\n";
        if (!ctx.resultPaths.vst3Bundle.empty()) t += L"VST3:  " + ctx.resultPaths.vst3Bundle + L"\r\n";
        if (!ctx.resultPaths.vst2Dll.empty())    t += L"VST2:  " + ctx.resultPaths.vst2Dll + L"\r\n";
        if (!ctx.resultPaths.clapFile.empty())   t += L"CLAP:  " + ctx.resultPaths.clapFile + L"\r\n";
    } else {
        t = L"Setup did not finish:\r\n\r\n" + ctx.installError;
    }
    return t;
}

std::wstring BrowseForFolder(HWND owner, const std::wstring& initial) {
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return result;

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Choose the VST2 plug-in folder");

    if (!initial.empty()) {
        IShellItem* initItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&initItem)))) {
            dlg->SetFolder(initItem);
            initItem->Release();
        }
    }

    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

DWORD WINAPI InstallThreadProc(LPVOID param) {
    WizardContext* ctx = reinterpret_cast<WizardContext*>(param);
    InstallOptions opts = BuildOptionsFromCtx(*ctx);

    HWND page = ctx->hInstallPage;
    ProgressFn cb = [page](int pct, const std::wstring& status) {
        std::wstring* s = new std::wstring(status);
        PostMessageW(page, WM_KK_PROGRESS, (WPARAM)pct, (LPARAM)s);
    };

    InstalledPaths paths;
    std::wstring err;
    bool ok = RunInstall(opts, cb, paths, err);

    ctx->resultPaths = paths;
    ctx->installOk = ok;
    ctx->installError = err;

    std::wstring* errCopy = new std::wstring(err);
    PostMessageW(page, WM_KK_DONE, (WPARAM)(ok ? 1 : 0), (LPARAM)errCopy);
    return 0;
}

WizardContext* CtxFromDlg(HWND hwndDlg) {
    return reinterpret_cast<WizardContext*>(GetWindowLongPtrW(hwndDlg, GWLP_USERDATA));
}

void StoreCtx(HWND hwndDlg, LPARAM initLParam) {
    PROPSHEETPAGEW* psp = reinterpret_cast<PROPSHEETPAGEW*>(initLParam);
    SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)psp->lParam);
}

// -------------------------------------------------------------------- pages

INT_PTR CALLBACK WelcomePageProc(HWND hwndDlg, UINT msg, WPARAM, LPARAM lParam) {
    if (msg == WM_INITDIALOG) { StoreCtx(hwndDlg, lParam); return TRUE; }
    if (msg == WM_NOTIFY && reinterpret_cast<LPNMHDR>(lParam)->code == PSN_SETACTIVE) {
        PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT);
        SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK ComponentsPageProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        StoreCtx(hwndDlg, lParam);
        WizardContext* ctx = CtxFromDlg(hwndDlg);
        CheckDlgButton(hwndDlg, IDC_CHECK_VST3, ctx->vst3 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwndDlg, IDC_CHECK_VST2, ctx->vst2 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwndDlg, IDC_CHECK_CLAP, ctx->clap ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = reinterpret_cast<LPNMHDR>(lParam);
        WizardContext* ctx = CtxFromDlg(hwndDlg);
        if (nm->code == PSN_SETACTIVE) {
            PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT) {
            ctx->vst3 = IsDlgButtonChecked(hwndDlg, IDC_CHECK_VST3) == BST_CHECKED;
            ctx->vst2 = IsDlgButtonChecked(hwndDlg, IDC_CHECK_VST2) == BST_CHECKED;
            ctx->clap = IsDlgButtonChecked(hwndDlg, IDC_CHECK_CLAP) == BST_CHECKED;
            if (!ctx->vst3 && !ctx->vst2 && !ctx->clap) {
                MessageBoxW(hwndDlg, L"Please select at least one plug-in format.", L"Kickarse Setup",
                            MB_OK | MB_ICONWARNING);
                SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
                return TRUE;
            }
            if (!ctx->vst2) {
                // no folder to choose -- go straight to the install page
                SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, IDD_INSTALLPAGE);
                return TRUE;
            }
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

INT_PTR CALLBACK Vst2DirPageProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        StoreCtx(hwndDlg, lParam);
        WizardContext* ctx = CtxFromDlg(hwndDlg);
        SetDlgItemTextW(hwndDlg, IDC_EDIT_VST2DIR, ctx->vst2Dir.c_str());
        SendMessage(GetDlgItem(hwndDlg, IDC_EDIT_VST2DIR), EM_LIMITTEXT, MAX_PATH - 1, 0);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BUTTON_BROWSE && HIWORD(wParam) == BN_CLICKED) {
            wchar_t buf[MAX_PATH] = {};
            GetDlgItemTextW(hwndDlg, IDC_EDIT_VST2DIR, buf, MAX_PATH);
            std::wstring chosen = BrowseForFolder(hwndDlg, buf);
            if (!chosen.empty()) SetDlgItemTextW(hwndDlg, IDC_EDIT_VST2DIR, chosen.c_str());
            return TRUE;
        }
        break;
    case WM_NOTIFY: {
        LPNMHDR nm = reinterpret_cast<LPNMHDR>(lParam);
        WizardContext* ctx = CtxFromDlg(hwndDlg);
        if (nm->code == PSN_SETACTIVE) {
            PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT) {
            wchar_t buf[MAX_PATH] = {};
            GetDlgItemTextW(hwndDlg, IDC_EDIT_VST2DIR, buf, MAX_PATH);
            std::wstring v = buf;
            if (v.empty()) {
                MessageBoxW(hwndDlg, L"Please choose a folder for the VST2 plug-in.", L"Kickarse Setup",
                            MB_OK | MB_ICONWARNING);
                SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, -1);
                return TRUE;
            }
            ctx->vst2Dir = v;
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

INT_PTR CALLBACK InstallPageProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        StoreCtx(hwndDlg, lParam);
        WizardContext* ctx = CtxFromDlg(hwndDlg);
        ctx->hInstallPage = hwndDlg;
        SendMessage(GetDlgItem(hwndDlg, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        return TRUE;
    }
    case WM_KK_PROGRESS: {
        std::wstring* status = reinterpret_cast<std::wstring*>(lParam);
        SendMessage(GetDlgItem(hwndDlg, IDC_PROGRESS), PBM_SETPOS, (WPARAM)wParam, 0);
        if (status) {
            SetDlgItemTextW(hwndDlg, IDC_STATIC_STATUS, status->c_str());
            delete status;
        }
        return TRUE;
    }
    case WM_KK_DONE: {
        WizardContext* ctx = CtxFromDlg(hwndDlg);
        bool ok = wParam != 0;
        std::wstring* errPtr = reinterpret_cast<std::wstring*>(lParam);
        std::wstring err = errPtr ? *errPtr : std::wstring();
        delete errPtr;

        ctx->installFinished = true;
        ctx->installOk = ok;
        ctx->installError = err;

        if (ok) {
            SendMessage(GetDlgItem(hwndDlg, IDC_PROGRESS), PBM_SETPOS, 100, 0);
            SetDlgItemTextW(hwndDlg, IDC_STATIC_STATUS, L"Done.");
            PropSheet_SetWizButtons(ctx->hSheet, PSWIZB_NEXT);
        } else {
            SetDlgItemTextW(hwndDlg, IDC_STATIC_STATUS, L"Setup could not finish.");
            MessageBoxW(hwndDlg, err.c_str(), L"Kickarse Setup", MB_OK | MB_ICONWARNING);
            ctx->installStarted = false; // allow Back -> Next to retry
            PropSheet_SetWizButtons(ctx->hSheet, PSWIZB_BACK);
        }
        return TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = reinterpret_cast<LPNMHDR>(lParam);
        WizardContext* ctx = CtxFromDlg(hwndDlg);
        if (nm->code == PSN_SETACTIVE) {
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
            if (!ctx->installStarted) {
                ctx->installStarted = true;
                ctx->installFinished = false;
                PropSheet_SetWizButtons(ctx->hSheet, 0); // no navigating away mid-install
                SetDlgItemTextW(hwndDlg, IDC_STATIC_STATUS, L"Starting...");
                SendMessage(GetDlgItem(hwndDlg, IDC_PROGRESS), PBM_SETPOS, 0, 0);

                InstallOptions opts = BuildOptionsFromCtx(*ctx);
                if (InstallNeedsAdmin(opts) && !IsProcessElevated()) {
                    std::wstring childArgs = BuildElevatedGuiArgs(*ctx);
                    DWORD werr = 0;
                    int code = RelaunchElevated(childArgs, /*waitForIt=*/false, &werr);
                    if (code < 0) {
                        MessageBoxW(hwndDlg,
                            L"Kickarse needs administrator rights to install into the selected folders.\r\n\r\n"
                            L"Please try again and approve the elevation prompt, or go Back and choose "
                            L"folders you already have write access to.",
                            L"Kickarse Setup", MB_OK | MB_ICONWARNING);
                        ctx->installStarted = false;
                        PropSheet_SetWizButtons(ctx->hSheet, PSWIZB_BACK);
                    } else {
                        // Handed off to the elevated instance; this one just closes.
                        PropSheet_PressButton(ctx->hSheet, PSBTN_CANCEL);
                    }
                } else {
                    CreateThread(nullptr, 0, InstallThreadProc, ctx, 0, nullptr);
                }
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

INT_PTR CALLBACK FinishPageProc(HWND hwndDlg, UINT msg, WPARAM, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        StoreCtx(hwndDlg, lParam);
        return TRUE;
    case WM_NOTIFY: {
        LPNMHDR nm = reinterpret_cast<LPNMHDR>(lParam);
        WizardContext* ctx = CtxFromDlg(hwndDlg);
        if (nm->code == PSN_SETACTIVE) {
            SetDlgItemTextW(hwndDlg, IDC_STATIC_FINISHTEXT, BuildFinishText(*ctx).c_str());
            PropSheet_SetWizButtons(ctx->hSheet, PSWIZB_FINISH);
            SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

int CALLBACK PropSheetProc(HWND hwndDlg, UINT uMsg, LPARAM) {
    if (uMsg == PSCB_INITIALIZED && g_ctx) {
        g_ctx->hSheet = hwndDlg;
        SetWindowTextW(hwndDlg, L"Kickarse Setup"); // classic wizards ignore pszCaption
    }
    return 0;
}

} // namespace

int RunInstallWizard(const ParsedArgs& args) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    bool comInited = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

    WizardContext ctx;
    ctx.initialArgs = args;
    ctx.vst3 = args.hasComponents ? args.vst3 : true;
    ctx.vst2 = args.hasComponents ? args.vst2 : true;
    ctx.clap = args.hasComponents ? args.clap : true;
    ctx.vst2Dir = args.vst2Dir.empty() ? GetDefaultVst2Dir() : args.vst2Dir;
    g_ctx = &ctx;

    PROPSHEETPAGEW pages[5] = {};
    for (auto& p : pages) {
        p.dwSize = sizeof(PROPSHEETPAGEW);
        p.hInstance = g_hInst;
        p.lParam = reinterpret_cast<LPARAM>(&ctx);
        p.dwFlags = PSP_DEFAULT;
    }
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_WELCOME);
    pages[0].pfnDlgProc = WelcomePageProc;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_COMPONENTS);
    pages[1].pfnDlgProc = ComponentsPageProc;
    pages[2].pszTemplate = MAKEINTRESOURCEW(IDD_VST2DIR);
    pages[2].pfnDlgProc = Vst2DirPageProc;
    pages[3].pszTemplate = MAKEINTRESOURCEW(IDD_INSTALLPAGE);
    pages[3].pfnDlgProc = InstallPageProc;
    pages[4].pszTemplate = MAKEINTRESOURCEW(IDD_FINISH);
    pages[4].pfnDlgProc = FinishPageProc;

    PROPSHEETHEADERW psh{};
    psh.dwSize = sizeof(psh);
    // PSH_PROPSHEETPAGE is required whenever ppsp points at a raw PROPSHEETPAGE array (as
    // opposed to an array of HPROPSHEETPAGE handles from CreatePropertySheetPage); without it
    // comctl32 misreads our struct array as handles and crashes inside COMCTL32.dll.
    // Classic wizard, not PSH_AEROWIZARD: the Aero variant rendered without its command area
    // (no Back/Next/Cancel) on Windows 11, leaving the user stuck on the first page.
    psh.dwFlags = PSH_WIZARD | PSH_USECALLBACK | PSH_USEHICON | PSH_PROPSHEETPAGE;
    psh.hInstance = g_hInst;
    psh.pszCaption = L"Kickarse Setup";
    psh.pfnCallback = PropSheetProc;
    psh.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APPICON));

    // A relaunched-elevated GUI resumes directly at Install -> Finish; no Back target exists.
    PROPSHEETPAGEW resumePages[2] = { pages[3], pages[4] };
    if (args.elevatedGui) {
        psh.ppsp = resumePages;
        psh.nPages = 2;
    } else {
        psh.ppsp = pages;
        psh.nPages = 5;
    }

    INT_PTR rc = PropertySheetW(&psh);

    g_ctx = nullptr;
    if (comInited) CoUninitialize();
    return (int)rc;
}

} // namespace kick
