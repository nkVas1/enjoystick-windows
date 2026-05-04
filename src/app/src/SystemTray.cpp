#include <enjoystick/app/SystemTray.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <stdexcept>

// Resource ID for the application icon compiled from enjoystick.rc
#define IDI_APP 101

namespace enjoystick::app {

static constexpr UINT WM_TRAY_CALLBACK  = WM_USER + 100;
static constexpr UINT WM_TRAY_MENUITEM  = WM_USER + 101;  ///< Posted to dispatch menu action safely
static constexpr UINT kTrayIconId       = 1;

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

class SystemTrayImpl final : public SystemTray {
public:
    SystemTrayImpl(std::wstring tooltip, std::wstring iconPath)
        : m_tooltip(std::move(tooltip))
        , m_iconPath(std::move(iconPath))
    {
        RegisterTrayWindowClass();
        CreateTrayWindow();
        LoadIcon_();
        AddTrayIcon();
    }

    ~SystemTrayImpl() override {
        Remove();
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        if (m_hIconOwned && m_hIcon) {
            DestroyIcon(m_hIcon);
            m_hIcon = nullptr;
        }
        UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
    }

    void SetMenuItems(std::vector<TrayMenuItem> items) override {
        std::lock_guard lock(m_mutex);
        m_items = std::move(items);
    }

    void SetTooltip(std::wstring tooltip) override {
        m_tooltip = std::move(tooltip);
        NOTIFYICONDATAW nid = BuildNID();
        wcsncpy_s(nid.szTip, m_tooltip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void ShowBalloon(std::wstring title, std::wstring text, uint32_t /*durationMs*/) override {
        NOTIFYICONDATAW nid = BuildNID();
        nid.uFlags         |= NIF_INFO;
        nid.dwInfoFlags     = NIIF_INFO | NIIF_NOSOUND;
        wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(nid.szInfo,      text.c_str(),  _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void SetOnDoubleClick(std::function<void()> callback) override {
        std::lock_guard lock(m_mutex);
        m_onDoubleClick = std::move(callback);
    }

    void Remove() override {
        if (!m_added) return;
        NOTIFYICONDATAW nid = BuildNID();
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_added = false;
    }

private:
    static constexpr const wchar_t* kClassName = L"EnjoyStickTrayHost";

    // -------------------------------------------------------------------------
    // Icon loading — priority:
    //   1. Resource IDI_APP=101 embedded in the EXE (compiled from .rc)
    //   2. File path supplied by caller (LoadImageW from disk)
    //   3. Windows stock IDI_APPLICATION
    // -------------------------------------------------------------------------
    void LoadIcon_() {
        // 1) Try resource icon (EXE module)
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        m_hIcon = static_cast<HICON>(
            LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP),
                       IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
        if (m_hIcon) { m_hIconOwned = true; return; }

        // 2) Try file path
        if (!m_iconPath.empty()) {
            m_hIcon = static_cast<HICON>(
                LoadImageW(nullptr, m_iconPath.c_str(),
                           IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
            if (m_hIcon) { m_hIconOwned = true; return; }
        }

        // 3) Stock fallback (not owned, never DestroyIcon)
        m_hIcon     = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
        m_hIconOwned = false;
    }

    NOTIFYICONDATAW BuildNID() const {
        NOTIFYICONDATAW nid  = {};
        nid.cbSize           = sizeof(nid);
        nid.hWnd             = m_hwnd;
        nid.uID              = kTrayIconId;
        nid.uCallbackMessage = WM_TRAY_CALLBACK;
        nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.hIcon            = m_hIcon
                                   ? m_hIcon
                                   : LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
        wcsncpy_s(nid.szTip, m_tooltip.c_str(), _TRUNCATE);
        return nid;
    }

    void RegisterTrayWindowClass() {
        // Unregister first in case a previous instance left it (e.g. debug re-run)
        UnregisterClassW(kClassName, GetModuleHandleW(nullptr));

        WNDCLASSEXW wc  = {};
        wc.cbSize       = sizeof(wc);
        wc.lpfnWndProc  = TrayWndProc;
        wc.hInstance    = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc))
            throw std::runtime_error("SystemTray: failed to register window class");
    }

    void CreateTrayWindow() {
        m_hwnd = CreateWindowExW(
            0, kClassName, L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), this);
        if (!m_hwnd) throw std::runtime_error("SystemTray: failed to create message window");
    }

    void AddTrayIcon() {
        NOTIFYICONDATAW nid = BuildNID();
        Shell_NotifyIconW(NIM_ADD, &nid);
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        m_added = true;
    }

    // -------------------------------------------------------------------------
    // ShowContextMenu
    // Must NOT be called with m_mutex held (TrackPopupMenu runs a nested
    // message loop which can re-enter WndProc).
    // -------------------------------------------------------------------------
    void ShowContextMenu() {
        // --- Snapshot items under lock, then release ---
        std::vector<TrayMenuItem> items;
        {
            std::lock_guard lock(m_mutex);
            items = m_items;  // copy
        }
        if (items.empty()) return;

        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        // Build HMENU.  Separators get ID=0; actionable items get ID = index+1.
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            if (item.separator || item.label.empty()) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            } else {
                UINT flags = MF_STRING;
                if (!item.action) flags |= MF_GRAYED;
                AppendMenuW(hMenu, flags,
                            static_cast<UINT_PTR>(i + 1),
                            item.label.c_str());
            }
        }

        // Bring message window to foreground so menu dismisses on click-away
        SetForegroundWindow(m_hwnd);

        POINT pt;
        GetCursorPos(&pt);

        // TrackPopupMenu internally pumps messages — that is safe here because
        // we are NOT holding m_mutex and we use TPM_RETURNCMD (no WM_COMMAND).
        const UINT chosen = static_cast<UINT>(TrackPopupMenu(
            hMenu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
            pt.x, pt.y, 0, m_hwnd, nullptr));
        DestroyMenu(hMenu);

        // Post the action index back to ourselves so it executes on the main
        // message loop, well outside the TrackPopupMenu call stack.
        if (chosen > 0 && chosen <= static_cast<UINT>(items.size())) {
            PostMessageW(m_hwnd, WM_TRAY_MENUITEM, static_cast<WPARAM>(chosen - 1), 0);
        }
    }

    // -------------------------------------------------------------------------
    static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }

        auto* self = reinterpret_cast<SystemTrayImpl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        // ---- Deferred menu action dispatch ------------------------------------
        if (msg == WM_TRAY_MENUITEM) {
            const size_t idx = static_cast<size_t>(wp);
            std::function<void()> action;
            {
                std::lock_guard lock(self->m_mutex);
                if (idx < self->m_items.size())
                    action = self->m_items[idx].action;
            }
            if (action) action();
            return 0;
        }

        // ---- Shell tray callback ---------------------------------------------
        if (msg == WM_TRAY_CALLBACK) {
            const UINT event = LOWORD(lp);

            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                // ShowContextMenu must run outside WndProc stack to allow
                // TrackPopupMenu's inner message loop to work safely.
                // We post to ourselves so the call happens after WndProc returns.
                PostMessageW(hwnd, WM_USER + 102, 0, 0);
                return 0;
            }

            if (event == WM_LBUTTONDBLCLK) {
                std::function<void()> cb;
                {
                    std::lock_guard lock(self->m_mutex);
                    cb = self->m_onDoubleClick;
                }
                if (cb) cb();
                return 0;
            }

            // Single left click — also show menu (common Windows UX pattern)
            if (event == WM_LBUTTONUP) {
                PostMessageW(hwnd, WM_USER + 102, 0, 0);
                return 0;
            }
        }

        // ---- Deferred ShowContextMenu trigger --------------------------------
        if (msg == WM_USER + 102) {
            self->ShowContextMenu();
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------
    std::wstring              m_tooltip;
    std::wstring              m_iconPath;
    HWND                      m_hwnd       = nullptr;
    HICON                     m_hIcon      = nullptr;
    bool                      m_hIconOwned = false;
    bool                      m_added      = false;
    std::mutex                m_mutex;
    std::vector<TrayMenuItem> m_items;
    std::function<void()>     m_onDoubleClick;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<SystemTray> SystemTray::Create(
    std::wstring tooltip, std::wstring iconResourcePath)
{
    return std::make_unique<SystemTrayImpl>(
        std::move(tooltip), std::move(iconResourcePath));
}

} // namespace enjoystick::app
