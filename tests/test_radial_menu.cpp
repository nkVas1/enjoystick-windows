#include <gtest/gtest.h>
#include <enjoystick/overlay/RadialMenu.hpp>
#include <cmath>

using namespace enjoystick;
using namespace enjoystick::overlay;

TEST(RadialMenu, DefaultPagesNotEmpty) {
    auto pages = RadialMenuController::BuildDefaultPages();
    ASSERT_FALSE(pages.empty());
    for (const auto& p : pages) {
        EXPECT_GT(p.sectorCount, 0u);
    }
}

TEST(RadialMenu, OpenClose) {
    auto ctrl = MakeDefaultOSK();
    EXPECT_FALSE(ctrl.IsOpen());
    ctrl.Open();
    EXPECT_TRUE(ctrl.IsOpen());
    ctrl.Close();
    EXPECT_FALSE(ctrl.IsOpen());
}

TEST(RadialMenu, PageCycleWithRB) {
    auto ctrl = MakeDefaultOSK();
    ctrl.Open();
    const uint32_t initial = ctrl.CurrentPage();
    const uint32_t pageCount = ctrl.PageCount();

    // Simulate RB press
    ControllerState s{};
    s.buttons = Button::RB;
    ctrl.Update(s);
    EXPECT_EQ(ctrl.CurrentPage(), (initial + 1) % pageCount);

    // Simulate RB release then LB press (go back)
    ControllerState s2{};
    s2.buttons = Button::None;
    ctrl.Update(s2);
    ControllerState s3{};
    s3.buttons = Button::LB;
    ctrl.Update(s3);
    EXPECT_EQ(ctrl.CurrentPage(), initial);
}

TEST(RadialMenu, CancelWithEast) {
    bool closeCalled = false;
    auto ctrl = MakeDefaultOSK();
    ctrl.SetCloseCallback([&] { closeCalled = true; });
    ctrl.Open();

    ControllerState s{};
    s.buttons = Button::East;
    ctrl.Update(s);
    EXPECT_FALSE(ctrl.IsOpen());
    EXPECT_TRUE(closeCalled);
}

TEST(RadialMenu, CommitFiresCallback) {
    std::wstring committed;
    auto ctrl = RadialMenuController(RadialMenuController::BuildDefaultPages());
    ctrl.SetCommitCallback([&](std::wstring_view chars) {
        committed = std::wstring(chars);
    });
    ctrl.Open();

    // Point left stick upward → sector 0
    ControllerState s{};
    s.leftStick  = {0.0f, 0.9f}; // up
    s.buttons    = Button::South; // confirm
    ctrl.Update(s);
    EXPECT_FALSE(committed.empty());
}
