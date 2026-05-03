#include <enjoystick/overlay/RadialMenu.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace enjoystick::overlay {

// ───────────────────────────────────────────────────────────────────────────
// RadialMenuController
// ───────────────────────────────────────────────────────────────────────────

RadialMenuController::RadialMenuController(std::vector<RadialPage> pages)
    : m_pages(std::move(pages)) {
    if (m_pages.empty()) throw std::invalid_argument("RadialMenuController: pages must not be empty");
}

void RadialMenuController::Update(const ControllerState& state) {
    if (!m_open) return;

    HandlePageSwitch(state);
    HandleSectorSelection(state);

    m_prevButtons = state.buttons;
    m_prevLB = HasButton(state.buttons, Button::LB);
    m_prevRB = HasButton(state.buttons, Button::RB);
}

void RadialMenuController::HandlePageSwitch(const ControllerState& state) {
    const bool lb = HasButton(state.buttons, Button::LB);
    const bool rb = HasButton(state.buttons, Button::RB);

    if (lb && !m_prevLB) {
        m_currentPage = (m_currentPage + m_pages.size() - 1) % m_pages.size();
        m_highlightedSector = kNoSelection;
    } else if (rb && !m_prevRB) {
        m_currentPage = (m_currentPage + 1) % m_pages.size();
        m_highlightedSector = kNoSelection;
    }
}

void RadialMenuController::HandleSectorSelection(const ControllerState& state) {
    const auto& page = m_pages[m_currentPage];

    // Update highlight from left stick
    m_highlightedSector = StickToSector(state.leftStick, page.sectorCount);

    // Confirm on South down-edge
    const bool southNow  = HasButton(state.buttons,     Button::South);
    const bool southPrev = HasButton(m_prevButtons,      Button::South);
    if (southNow && !southPrev) {
        if (m_highlightedSector >= 0 &&
            m_highlightedSector < static_cast<int32_t>(page.sectorCount)) {
            const auto& sector = page.sectors[m_highlightedSector];
            if (m_onCommit && !sector.characters.empty()) {
                m_onCommit(sector.characters);
            }
        }
    }

    // Cancel on East down-edge
    const bool eastNow  = HasButton(state.buttons,  Button::East);
    const bool eastPrev = HasButton(m_prevButtons,   Button::East);
    if (eastNow && !eastPrev) {
        Close();
        if (m_onClose) m_onClose();
    }
}

int32_t RadialMenuController::StickToSector(Vec2 stick, uint32_t sectorCount) const noexcept {
    constexpr float kDeadzone = 0.35f;
    if (stick.x * stick.x + stick.y * stick.y < kDeadzone * kDeadzone) {
        return kNoSelection;
    }

    // atan2 gives angle in (-π, π]; we remap to [0, 2π)
    float angle = std::atan2(-stick.y, stick.x); // invert Y (screen coords)
    if (angle < 0.0f) angle += 2.0f * static_cast<float>(std::numbers::pi);

    // Rotate so sector 0 starts at top (subtract 90° = π/2)
    angle -= static_cast<float>(std::numbers::pi) / 2.0f;
    if (angle < 0.0f) angle += 2.0f * static_cast<float>(std::numbers::pi);

    const float sectorAngle = 2.0f * static_cast<float>(std::numbers::pi) / static_cast<float>(sectorCount);
    return static_cast<int32_t>(angle / sectorAngle) % static_cast<int32_t>(sectorCount);
}

// ───────────────────────────────────────────────────────────────────────────
// Default QWERTY pages
// ───────────────────────────────────────────────────────────────────────────

static RadialSector MakeCharSector(std::wstring ch) {
    RadialSector s;
    s.label      = ch;
    s.characters = std::move(ch);
    return s;
}

static RadialSector MakeActionSector(std::wstring label, std::wstring chars) {
    RadialSector s;
    s.label      = std::move(label);
    s.characters = std::move(chars);
    s.isAction   = true;
    return s;
}

std::vector<RadialPage> RadialMenuController::BuildDefaultPages() {
    // Page 0: Lowercase (8 sectors, 3 chars each — cycle with RS or tap again)
    // For simplicity each sector maps to its primary letter; a full impl would
    // use sub-menus per sector for multi-char groups.
    RadialPage lower;
    lower.name = L"abc";
    lower.sectors[0] = MakeCharSector(L"a");
    lower.sectors[1] = MakeCharSector(L"e");
    lower.sectors[2] = MakeCharSector(L"i");
    lower.sectors[3] = MakeCharSector(L"o");
    lower.sectors[4] = MakeCharSector(L"u");
    lower.sectors[5] = MakeCharSector(L"s");
    lower.sectors[6] = MakeCharSector(L"t");
    lower.sectors[7] = MakeCharSector(L"n");
    lower.sectorCount = 8;

    // Page 1: More consonants
    RadialPage cons;
    cons.name = L"bcd";
    cons.sectors[0] = MakeCharSector(L"b");
    cons.sectors[1] = MakeCharSector(L"c");
    cons.sectors[2] = MakeCharSector(L"d");
    cons.sectors[3] = MakeCharSector(L"f");
    cons.sectors[4] = MakeCharSector(L"g");
    cons.sectors[5] = MakeCharSector(L"h");
    cons.sectors[6] = MakeCharSector(L"j");
    cons.sectors[7] = MakeCharSector(L"k");
    cons.sectorCount = 8;

    // Page 2: More consonants
    RadialPage cons2;
    cons2.name = L"lmn";
    cons2.sectors[0] = MakeCharSector(L"l");
    cons2.sectors[1] = MakeCharSector(L"m");
    cons2.sectors[2] = MakeCharSector(L"p");
    cons2.sectors[3] = MakeCharSector(L"q");
    cons2.sectors[4] = MakeCharSector(L"r");
    cons2.sectors[5] = MakeCharSector(L"v");
    cons2.sectors[6] = MakeCharSector(L"w");
    cons2.sectors[7] = MakeCharSector(L"x");
    cons2.sectorCount = 8;

    // Page 3: Uppercase toggle + symbols
    RadialPage sym;
    sym.name = L"!@#";
    sym.sectors[0] = MakeCharSector(L"1");
    sym.sectors[1] = MakeCharSector(L"2");
    sym.sectors[2] = MakeCharSector(L"3");
    sym.sectors[3] = MakeCharSector(L"4");
    sym.sectors[4] = MakeCharSector(L"5");
    sym.sectors[5] = MakeCharSector(L"!");
    sym.sectors[6] = MakeCharSector(L"?");
    sym.sectors[7] = MakeCharSector(L".");
    sym.sectorCount = 8;

    // Page 4: Actions
    RadialPage actions;
    actions.name = L"ctrl";
    actions.sectors[0] = MakeActionSector(L"⌫",   L"\b");     // Backspace
    actions.sectors[1] = MakeActionSector(L"⏎",   L"\r");     // Enter
    actions.sectors[2] = MakeActionSector(L"Tab",  L"\t");     // Tab
    actions.sectors[3] = MakeActionSector(L"Esc",  L"\x1B");   // Escape
    actions.sectors[4] = MakeActionSector(L"Spc",  L" ");      // Space
    actions.sectors[5] = MakeActionSector(L",",    L",");
    actions.sectors[6] = MakeActionSector(L"-",    L"-");
    actions.sectors[7] = MakeActionSector(L"@",    L"@");
    actions.sectorCount = 8;

    return {lower, cons, cons2, sym, actions};
}

// ───────────────────────────────────────────────────────────────────────────
// Text injection
// ───────────────────────────────────────────────────────────────────────────

void InjectText(std::wstring_view text) {
    if (text.empty()) return;

    std::vector<INPUT> inputs;
    inputs.reserve(text.size() * 2);

    for (wchar_t ch : text) {
        // Special virtual-key characters
        WORD vk = 0;
        if      (ch == L'\b')   vk = VK_BACK;
        else if (ch == L'\r')   vk = VK_RETURN;
        else if (ch == L'\t')   vk = VK_TAB;
        else if (ch == L'\x1B') vk = VK_ESCAPE;

        if (vk != 0) {
            INPUT down{}, up{};
            down.type       = INPUT_KEYBOARD;
            down.ki.wVk     = vk;
            up.type         = INPUT_KEYBOARD;
            up.ki.wVk       = vk;
            up.ki.dwFlags   = KEYEVENTF_KEYUP;
            inputs.push_back(down);
            inputs.push_back(up);
        } else {
            // Unicode injection: works for any character regardless of keyboard layout
            INPUT down{}, up{};
            down.type         = INPUT_KEYBOARD;
            down.ki.wVk       = 0;
            down.ki.wScan     = ch;
            down.ki.dwFlags   = KEYEVENTF_UNICODE;
            up               = down;
            up.ki.dwFlags    = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            inputs.push_back(down);
            inputs.push_back(up);
        }
    }

    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

RadialMenuController MakeDefaultOSK() {
    auto ctrl = RadialMenuController(RadialMenuController::BuildDefaultPages());
    ctrl.SetCommitCallback([](std::wstring_view chars) {
        InjectText(chars);
    });
    return ctrl;
}

} // namespace enjoystick::overlay
