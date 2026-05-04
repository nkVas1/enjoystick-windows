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
#include <cstring>
#include <cstdlib>

// Resource ID for the application icon (fallback if memory load fails)
#define IDI_APP 101

namespace enjoystick::app {

static constexpr UINT WM_TRAY_CALLBACK = WM_USER + 100;
static constexpr UINT WM_TRAY_MENUITEM = WM_USER + 101;
static constexpr UINT WM_SHOW_MENU     = WM_USER + 102;
static constexpr UINT kTrayIconId      = 1;

// ---------------------------------------------------------------------------
// LoadIconFromIcoBlob
// Parses a raw .ico file in memory and creates an HICON for the frame
// closest to (targetW x targetH). Returns nullptr on failure.
// ---------------------------------------------------------------------------
static HICON LoadIconFromIcoBlob(
    const uint8_t* data, uint32_t size,
    int targetW = 0, int targetH = 0) noexcept
{
    if (!data || size < 6) return nullptr;

    // ICO header
    uint16_t reserved, type, count;
    std::memcpy(&reserved, data + 0, 2);
    std::memcpy(&type,     data + 2, 2);
    std::memcpy(&count,    data + 4, 2);
    if (reserved != 0 || type != 1 || count == 0) return nullptr;

    if (targetW == 0) targetW = GetSystemMetrics(SM_CXICON);
    if (targetH == 0) targetH = GetSystemMetrics(SM_CYICON);

    // ICONDIRENTRY: 1 byte width, 1 height, 1 colorCount, 1 reserved,
    //               2 planes, 2 bitCount, 4 sizeInBytes, 4 imageOffset
    struct DirEntry { uint8_t w, h, clr, rsv; uint16_t planes, bpp; uint32_t sz, off; };
    static_assert(sizeof(DirEntry) == 16, "DirEntry must be 16 bytes");

    const uint32_t dirStart = 6;
    if (size < dirStart + count * 16u) return nullptr;

    // Find best-fit frame (prefer exact match, otherwise closest size)
    int    bestIdx  = 0;
    int    bestDiff = INT_MAX;
    for (int i = 0; i < static_cast<int>(count); ++i) {
        DirEntry e;
        std::memcpy(&e, data + dirStart + i * 16, 16);
        int w = e.w == 0 ? 256 : e.w;
        int h = e.h == 0 ? 256 : e.h;
        int diff = std::abs(w - targetW) + std::abs(h - targetH);
        if (diff < bestDiff) { bestDiff = diff; bestIdx = i; }
    }

    DirEntry best;
    std::memcpy(&best, data + dirStart + bestIdx * 16, 16);
    if (best.off + best.sz > size) return nullptr;

    return CreateIconFromResourceEx(
        // MSDN: lpbIconBits must point to the DIB bits (or PNG) of the icon.
        // For ICO files this is exactly the per-frame data at best.off.
        const_cast<PBYTE>(data + best.off), best.sz,
        TRUE /*fIcon*/,
        0x00030000 /*dwVersion = 3.0*/,
        0, 0,
        LR_DEFAULTCOLOR);
}

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
    //   1. Embedded ICO blob compiled into the header (always available)
    //   2. RC resource IDI_APP=101 in the EXE
    //   3. File on disk (iconPath parameter)
    //   4. Windows stock IDI_APPLICATION
    // -------------------------------------------------------------------------
    void LoadIcon_() {
        // 1) Embedded ICO blob
        m_hIcon = LoadIconFromIcoBlob(
            kEnjoyStickIconData,
            kEnjoyStickIconSize,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON));
        if (m_hIcon) { m_hIconOwned = true; return; }

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

        // 4) Stock fallback
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
        WNDCLASSEXW wc   = {};
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
    // ShowContextMenu
    // Items are copied under lock, then lock is released before
    // TrackPopupMenu (which runs a nested message loop).
    // The chosen action is dispatched via PostMessage to the main loop.
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

        // Deferred menu action (posted from ShowContextMenu after TrackPopupMenu)
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

        // Deferred ShowContextMenu trigger (escapes WndProc stack)
        if (msg == WM_SHOW_MENU) {
            self->ShowContextMenu();
            return 0;
        }

        // Shell tray notifications
        if (msg == WM_TRAY_CALLBACK) {
            const UINT event = LOWORD(lp);
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU || event == WM_LBUTTONUP) {
                PostMessageW(hwnd, WM_SHOW_MENU, 0, 0);
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
