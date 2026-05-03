#pragma once

#include <enjoystick/shared/Types.hpp>
#include <array>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace enjoystick::overlay {

/// Maximum number of sectors in one radial ring.
constexpr uint32_t kMaxSectors = 8;

///
/// RadialMenu — controller-driven radial selection widget.
///
/// Layout model:
///   - Up to 8 sectors arranged around a circle, mapped to stick angle.
///   - Each sector holds a label (glyph or string) and an action callback.
///   - Pressing confirm (South / A / Cross) fires the highlighted sector.
///   - Pressing cancel (East / B / Circle) closes without action.
///   - Pages: holding LB/RB switches the active character page (lower, upper, symbols).
///
struct RadialSector {
    std::wstring label;          ///< Display label (single glyph preferred)
    std::wstring characters;     ///< Characters injected on confirm (may be multi-char)
    bool         isAction = false; ///< True for non-character actions (Backspace, Enter, etc.)
};

struct RadialPage {
    std::wstring              name;    ///< Page name shown in centre hub
    std::array<RadialSector, kMaxSectors> sectors = {};
    uint32_t                  sectorCount = 0;
};

///
/// RadialMenuController — pure logic layer (no rendering, no Win32).
/// Rendering is handled separately in OverlayRenderer.
///
class RadialMenuController {
public:
    using CommitCallback = std::function<void(std::wstring_view characters)>;
    using CloseCallback  = std::function<void()>;

    explicit RadialMenuController(std::vector<RadialPage> pages);

    /// Feed a controller state each frame.
    void Update(const ControllerState& state);

    void SetCommitCallback(CommitCallback cb) { m_onCommit = std::move(cb); }
    void SetCloseCallback(CloseCallback  cb)  { m_onClose  = std::move(cb); }

    void Open()  noexcept { m_open = true;  m_highlightedSector = kNoSelection; }
    void Close() noexcept { m_open = false; }
    [[nodiscard]] bool IsOpen() const noexcept { return m_open; }

    /// Index of the currently highlighted sector (-1 = centre / none).
    [[nodiscard]] int32_t HighlightedSector() const noexcept { return m_highlightedSector; }
    [[nodiscard]] uint32_t CurrentPage()      const noexcept { return m_currentPage; }
    [[nodiscard]] const RadialPage& GetPage(uint32_t idx) const { return m_pages.at(idx); }
    [[nodiscard]] const RadialPage& CurrentPageData()     const { return m_pages.at(m_currentPage); }
    [[nodiscard]] uint32_t PageCount()                    const noexcept { return static_cast<uint32_t>(m_pages.size()); }

    /// Build the default QWERTY-inspired pages.
    [[nodiscard]] static std::vector<RadialPage> BuildDefaultPages();

private:
    static constexpr int32_t kNoSelection = -1;

    void HandlePageSwitch(const ControllerState& state);
    void HandleSectorSelection(const ControllerState& state);
    [[nodiscard]] int32_t StickToSector(Vec2 stick, uint32_t sectorCount) const noexcept;

    std::vector<RadialPage> m_pages;
    uint32_t  m_currentPage       = 0;
    int32_t   m_highlightedSector = kNoSelection;
    bool      m_open              = false;

    // Edge detection for confirm/cancel buttons
    Button m_prevButtons = Button::None;
    // Edge detection for page switch buttons
    bool   m_prevLB = false;
    bool   m_prevRB = false;
};

/// Inject characters / control sequences into the focused window via SendInput.
void InjectText(std::wstring_view text);

/// Convenience: build a RadialMenuController wired to InjectText.
[[nodiscard]] RadialMenuController MakeDefaultOSK();

} // namespace enjoystick::overlay
