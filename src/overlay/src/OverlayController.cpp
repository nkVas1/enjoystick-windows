#include <enjoystick/overlay/OverlayController.hpp>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
OverlayController::OverlayController(OverlayWindow* window)
    : m_window(window)
{
    if (!m_window) return;

    // Wire keyboard submit / close to update focus
    auto& kb = m_window->GetVirtualKeyboard();
    kb.SetOnSubmit([this](const std::wstring& text) {
        m_focus = FocusOwner::None;
        if (m_onText) m_onText(text);
    });

    // Wire settings changed
    // (SettingsMenu callback is set via SettingsMenu constructor;
    //  we re-route through OverlayController for a uniform API)
    // Note: SettingsMenu::OnChangedCallback is already set in OverlayWindow
    // during construction. We chain here by intercepting via SetupDefaultRadialItems.
}

// ---------------------------------------------------------------------------
void OverlayController::SetRadialItems(std::vector<RadialMenuItem> items) {
    WireRadialItemActions(items);
    m_window->GetRadialMenu().SetItems(std::move(items));
}

// ---------------------------------------------------------------------------
void OverlayController::SetupDefaultRadialItems() {
    // Default 6-item layout
    std::vector<RadialMenuItem> items;
    items.reserve(6);

    RadialMenuItem kb;
    kb.id    = L"keyboard";
    kb.label = L"Keyboard";
    kb.icon  = L"\u2328";   // ⌨ (keyboard emoji)
    items.push_back(std::move(kb));

    RadialMenuItem sets;
    sets.id    = L"settings";
    sets.label = L"Settings";
    sets.icon  = L"\u2699";  // ⚙ (gear emoji)
    items.push_back(std::move(sets));

    RadialMenuItem cut;
    cut.id    = L"cut";
    cut.label = L"Cut";
    cut.icon  = L"\u2702";   // ✂
    items.push_back(std::move(cut));

    RadialMenuItem cpy;
    cpy.id    = L"copy";
    cpy.label = L"Copy";
    cpy.icon  = L"\uD83D\uDCCB"; // 📋 clipboard
    items.push_back(std::move(cpy));

    RadialMenuItem pst;
    pst.id    = L"paste";
    pst.label = L"Paste";
    pst.icon  = L"\uD83D\uDCCC"; // 📌 pushpin (reused for paste)
    items.push_back(std::move(pst));

    RadialMenuItem undo;
    undo.id    = L"undo";
    undo.label = L"Undo";
    undo.icon  = L"\u21BA";  // ↺ counter-clockwise arrow
    items.push_back(std::move(undo));

    SetRadialItems(std::move(items));
}

// ---------------------------------------------------------------------------
void OverlayController::WireRadialItemActions(std::vector<RadialMenuItem>& items) {
    for (auto& item : items) {
        if (item.id == L"keyboard") {
            item.action = [this] { OpenKeyboard(); };
        } else if (item.id == L"settings") {
            item.action = [this] { OpenSettings(); };
        }
        // Other items can have their actions set by the caller; we don’t
        // overwrite a non-null action.
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
    // Open with current values (callers should push their values before
    // calling SetupDefaultRadialItems, or subscribe OnSettingsChanged).
    SettingsMenu::Values defaults{};
    m_window->GetSettingsMenu().Open(defaults);
}

// ---------------------------------------------------------------------------
void OverlayController::PostState(const ControllerState& state) {
    if (!m_window) return;

    // Track focus owner based on what’s actually open
    const bool kbOpen   = m_window->GetVirtualKeyboard().IsOpen();
    const bool setOpen  = m_window->GetSettingsMenu().IsOpen();
    const bool radOpen  = m_window->GetRadialMenu().IsVisible();

    if      (kbOpen)  m_focus = FocusOwner::Keyboard;
    else if (setOpen) m_focus = FocusOwner::Settings;
    else if (radOpen) m_focus = FocusOwner::RadialMenu;
    else              m_focus = FocusOwner::None;

    m_window->PostState(state);
}

} // namespace enjoystick::overlay
