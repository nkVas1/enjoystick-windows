#include <enjoystick/app/SystemTray.hpp>
#include "../res/enjoystick_icon_data.h"

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

// Resource ID for the application icon (fallback if memory load fails)
#define IDI_APP 101

namespace enjoystick::app {

static constexpr UINT WM_TRAY_CALLBACK  = WM_USER + 100;
static constexpr UINT WM_TRAY_MENUITEM  = WM_USER + 101;
static constexpr UINT WM_SHOW_MENU      = WM_USER + 102;
static constexpr UINT kTrayIconId       = 1;

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

    void ShowBalloon(std::wstring title, std::wstring text, uint32_t /*ms*/) override {
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
    // LoadIcon_
    // Priority:
    //   1. Memory-embedded ICO blob (kEnjoyStickIconData) — always available
    //   2. RC resource IDI_APP=101 in the EXE
    //   3. File on disk (iconPath parameter)
    //   4. Windows stock IDI_APPLICATION
    // -------------------------------------------------------------------------
    void LoadIcon_() {
        // 1) Try loading from embedded ICO bytes via CreateIconFromResourceEx.
        //    We need to find the image data for the appropriate size inside the
        //    ICO blob. For tray icons Windows requests 16x16 or 32x32.
        //    We pass the whole ICO blob header pointer; Windows extracts the
        //    correct frame internally when we use LookupIconIdFromDirectoryEx
        //    + CreateIconFromResourceEx.
        {
            const BYTE* pDir = kEnjoyStickIconData.data();
            // Skip ICO header (6 bytes) to get to first ICONDIRENTRY.
            // LookupIconIdFromDirectoryEx finds the best-fit frame index.
            const int offset = LookupIconIdFromDirectoryEx(
                const_cast<PBYTE>(pDir), TRUE,
                GetSystemMetrics(SM_CXSMICON),
                GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR);
            if (offset > 0) {
                m_hIcon = CreateIconFromResourceEx(
                    const_cast<PBYTE>(pDir + offset),
                    static_cast<DWORD>(kEnjoyStickIconSize - static_cast<uint32_t>(offset)),
                    TRUE, 0x00030000,
                    0, 0, LR_DEFAULTCOLOR | LR_DEFAULTSIZE);
                if (m_hIcon) { m_hIconOwned = true; return; }
            }
        }

        // 2) RC resource
        {
            HINSTANCE hInst = GetModuleHandleW(nullptr);
            m_hIcon = static_cast<HICON>(
                LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP),
                           IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
            if (m_hIcon) { m_hIconOwned = true; return; }
        }

        // 3) File path
        if (!m_iconPath.empty()) {
            m_hIcon = static_cast<HICON>(
                LoadImageW(nullptr, m_iconPath.c_str(),
                           IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
            if (m_hIcon) { m_hIconOwned = true; return; }
        }

        // 4) Stock fallback (system-owned, never DestroyIcon)
        m_hIcon      = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
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
        UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
        WNDCLASSEXW wc  = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = TrayWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
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
    // ShowContextMenu — must NOT be called with m_mutex held.
    // TrackPopupMenu runs a nested message loop; we copy items first,
    // release the lock, then call TrackPopupMenu outside the lock.
    // The action is dispatched via PostMessage so it runs on the main
    // message loop, not inside the TrackPopupMenu call stack.
    // -------------------------------------------------------------------------
    void ShowContextMenu() {
        std::vector<TrayMenuItem> items;
        {
            std::lock_guard lock(m_mutex);
            items = m_items;
        }
        if (items.empty()) return;

        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

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

        SetForegroundWindow(m_hwnd);
        POINT pt;
        GetCursorPos(&pt);

        const UINT chosen = static_cast<UINT>(TrackPopupMenu(
            hMenu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
            pt.x, pt.y, 0, m_hwnd, nullptr));
        DestroyMenu(hMenu);

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

        // Deferred menu action
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

        // Deferred ShowContextMenu (posted from tray callback to escape WndProc stack)
        if (msg == WM_SHOW_MENU) {
            self->ShowContextMenu();
            return 0;
        }

        // Shell tray icon events
        if (msg == WM_TRAY_CALLBACK) {
            const UINT event = LOWORD(lp);

            // Right-click or context menu key
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                PostMessageW(hwnd, WM_SHOW_MENU, 0, 0);
                return 0;
            }
            // Left single click — also show menu (Windows tray UX convention)
            if (event == WM_LBUTTONUP) {
                PostMessageW(hwnd, WM_SHOW_MENU, 0, 0);
                return 0;
            }
            // Left double-click — fire OnDoubleClick callback
            if (event == WM_LBUTTONDBLCLK) {
                std::function<void()> cb;
                {
                    std::lock_guard lock(self->m_mutex);
                    cb = self->m_onDoubleClick;
                }
                if (cb) cb();
                return 0;
            }
        }

        return DefWindowProcW(hwnd, msg, wp, lp);
    }

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
std::unique_ptr<SystemTray> SystemTray::Create(
    std::wstring tooltip, std::wstring iconResourcePath)
{
    return std::make_unique<SystemTrayImpl>(
        std::move(tooltip), std::move(iconResourcePath));
}

} // namespace enjoystick::app
