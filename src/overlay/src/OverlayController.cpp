#include <enjoystick/overlay/OverlayController.hpp>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
OverlayController::OverlayController(OverlayWindow* window)
    : m_window(window)
{
    if (!m_window) return;

    // Wire keyboard submit callback
    m_window->GetVirtualKeyboard().SetOnSubmit([this](const std::wstring& text) {
        m_focus = FocusOwner::None;
        if (m_onText) m_onText(text);
    });

    // Wire settings changed callback so m_currentSettings stays in sync
    m_window->GetSettingsMenu().SetOnChanged(
        [this](const SettingsMenu::Values& v) {
            m_currentSettings = v;
            if (m_onSettings) m_onSettings(v);
        });
}

// ---------------------------------------------------------------------------
void OverlayController::SetModeLabel(std::wstring label) {
    m_pendingModeLabel = label;
    if (m_window) m_window->SetModeLabel(std::move(label));
}

// ---------------------------------------------------------------------------
void OverlayController::SetRadialItems(std::vector<RadialMenuItem> items) {
    WireRadialItemActions(items);
    m_window->GetRadialMenu().SetItems(std::move(items));
}

// ---------------------------------------------------------------------------
void OverlayController::SetupDefaultRadialItems() {
    std::vector<RadialMenuItem> items;
    items.reserve(8);

    { RadialMenuItem i; i.id = L"keyboard";  i.label = L"Keyboard";  i.icon = L"\u2328";         items.push_back(std::move(i)); }
    { RadialMenuItem i; i.id = L"settings";  i.label = L"Settings";  i.icon = L"\u2699";         items.push_back(std::move(i)); }
    { RadialMenuItem i; i.id = L"controls";  i.label = L"Controls";  i.icon = L"\u2753";         items.push_back(std::move(i)); }
    { RadialMenuItem i; i.id = L"copy";      i.label = L"Copy";      i.icon = L"\uD83D\uDCCB";   items.push_back(std::move(i)); }
    { RadialMenuItem i; i.id = L"paste";     i.label = L"Paste";     i.icon = L"\uD83D\uDCCC";   items.push_back(std::move(i)); }
    { RadialMenuItem i; i.id = L"cut";       i.label = L"Cut";       i.icon = L"\u2702";         items.push_back(std::move(i)); }
    { RadialMenuItem i; i.id = L"undo";      i.label = L"Undo";      i.icon = L"\u21BA";         items.push_back(std::move(i)); }
    { RadialMenuItem i; i.id = L"redo";      i.label = L"Redo";      i.icon = L"\u21BB";         items.push_back(std::move(i)); }

    SetRadialItems(std::move(items));
}

// ---------------------------------------------------------------------------
void OverlayController::WireRadialItemActions(std::vector<RadialMenuItem>& items) {
    for (auto& item : items) {
        // Do not overwrite an action already set by the caller
        if (item.action) continue;

        if (item.id == L"keyboard") {
            item.action = [this] { OpenKeyboard(); };
        } else if (item.id == L"settings") {
            item.action = [this] { OpenSettings(); };
        } else if (item.id == L"controls") {
            item.action = [this] { OpenControls(); };
        } else if (item.id == L"cut") {
            item.action = [] {
                // Ctrl+X
                INPUT in[4]{};
                in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
                in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'X';
                in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'X';        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
                in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
                ::SendInput(4, in, sizeof(INPUT));
            };
        } else if (item.id == L"copy") {
            item.action = [] {
                INPUT in[4]{};
                in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
                in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'C';
                in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'C';        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
                in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
                ::SendInput(4, in, sizeof(INPUT));
            };
        } else if (item.id == L"paste") {
            item.action = [] {
                INPUT in[4]{};
                in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
                in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'V';
                in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'V';        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
                in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
                ::SendInput(4, in, sizeof(INPUT));
            };
        } else if (item.id == L"undo") {
            item.action = [] {
                INPUT in[4]{};
                in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
                in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'Z';
                in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'Z';        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
                in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
                ::SendInput(4, in, sizeof(INPUT));
            };
        } else if (item.id == L"redo") {
            item.action = [] {
                INPUT in[6]{};
                in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
                in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_SHIFT;
                in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'Z';
                in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = 'Z';        in[3].ki.dwFlags = KEYEVENTF_KEYUP;
                in[4].type = INPUT_KEYBOARD; in[4].ki.wVk = VK_SHIFT;   in[4].ki.dwFlags = KEYEVENTF_KEYUP;
                in[5].type = INPUT_KEYBOARD; in[5].ki.wVk = VK_CONTROL; in[5].ki.dwFlags = KEYEVENTF_KEYUP;
                ::SendInput(6, in, sizeof(INPUT));
            };
        }
    }
}

// ---------------------------------------------------------------------------
void OverlayController::OpenKeyboard() {
    if (!m_window) return;
    m_focus = FocusOwner::Keyboard;
    m_window->GetVirtualKeyboard().Open();
}

void OverlayController::OpenSettings() {
    if (!m_window) return;
    m_focus = FocusOwner::Settings;
    // Open with the last-known settings values so the panel shows real state
    m_window->GetSettingsMenu().Open(m_currentSettings);
}

void OverlayController::OpenControls() {
    if (!m_window) return;
    m_focus = FocusOwner::Controls;
    m_window->GetControlsOverlay().Open();
}

// ---------------------------------------------------------------------------
void OverlayController::PostState(const ControllerState& state) {
    if (!m_window) return;

    // Track focus owner based on which panel is actually open
    const bool kbOpen  = m_window->GetVirtualKeyboard().IsOpen();
    const bool setOpen = m_window->GetSettingsMenu().IsOpen();
    const bool radOpen = m_window->GetRadialMenu().IsVisible();
    const bool ctrlOpen= m_window->GetControlsOverlay().IsOpen();

    if      (ctrlOpen) m_focus = FocusOwner::Controls;
    else if (kbOpen)   m_focus = FocusOwner::Keyboard;
    else if (setOpen)  m_focus = FocusOwner::Settings;
    else if (radOpen)  m_focus = FocusOwner::RadialMenu;
    else               m_focus = FocusOwner::None;

    m_window->PostState(state);
}

} // namespace enjoystick::overlay
