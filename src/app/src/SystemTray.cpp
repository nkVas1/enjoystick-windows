#include <enjoystick/app/SystemTray.hpp>

#include <Windows.h>
#include <shellapi.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <algorithm>

namespace enjoystick::app {

static constexpr UINT WM_TRAY_CALLBACK = WM_USER + 100;
static constexpr UINT kTrayIconId      = 1;

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
        AddTrayIcon();
    }

    ~SystemTrayImpl() override {
        Remove();
        if (m_hwnd) DestroyWindow(m_hwnd);
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

    void Remove() override {
        if (!m_added) return;
        NOTIFYICONDATAW nid = BuildNID();
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_added = false;
    }

private:
    static constexpr const wchar_t* kClassName = L"EnjoyStickTrayHost";

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
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = TrayWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
    }

    void CreateTrayWindow() {
        m_hwnd = CreateWindowExW(
            0, kClassName, L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), this);
        if (!m_hwnd) throw std::runtime_error("SystemTray: failed to create message window");
    }

    void AddTrayIcon() {
        if (!m_iconPath.empty()) {
            m_hIcon = static_cast<HICON>(LoadImageW(
                nullptr, m_iconPath.c_str(),
                IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        }
        NOTIFYICONDATAW nid = BuildNID();
        Shell_NotifyIconW(NIM_ADD, &nid);

        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        m_added = true;
    }

    void ShowContextMenu() {
        std::lock_guard lock(m_mutex);
        if (m_items.empty()) return;

        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        for (size_t i = 0; i < m_items.size(); ++i) {
            const auto& item = m_items[i];
            if (item.separator) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            } else {
                AppendMenuW(hMenu, MF_STRING,
                            static_cast<UINT_PTR>(i + 1),
                            item.label.c_str());
            }
        }

        SetForegroundWindow(m_hwnd);

        POINT pt;
        GetCursorPos(&pt);
        const UINT chosen = static_cast<UINT>(TrackPopupMenu(
            hMenu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, m_hwnd, nullptr));
        DestroyMenu(hMenu);

        if (chosen > 0 && chosen <= m_items.size()) {
            const auto& action = m_items[chosen - 1].action;
            if (action) action();
        }
    }

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

        if (msg == WM_TRAY_CALLBACK) {
            const UINT event = LOWORD(lp);
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                self->ShowContextMenu();
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    std::wstring              m_tooltip;
    std::wstring              m_iconPath;
    HWND                      m_hwnd  = nullptr;
    HICON                     m_hIcon = nullptr;
    bool                      m_added = false;
    std::mutex                m_mutex;
    std::vector<TrayMenuItem> m_items;
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
