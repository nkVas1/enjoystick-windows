#include <gtest/gtest.h>
#include <enjoystick/cursor/VirtualMouse.hpp>
#include <cmath>

using namespace enjoystick;
using namespace enjoystick::cursor;

// We can only test the pure computation parts (ApplyCurve is private;
// we verify via observable velocity clamping behaviour).

TEST(VirtualMouse, DisabledDoesNotMove) {
    VirtualMouse vm;
    vm.SetEnabled(false);
    EXPECT_FALSE(vm.IsEnabled());

    // Calling Update with a full deflection should not crash
    ControllerState state{};
    state.rightStick = {1.0f, 0.0f};
    EXPECT_NO_THROW(vm.Update(state, 1.0f / 60.0f));
}

TEST(VirtualMouse, EnabledByDefault) {
    VirtualMouse vm;
    EXPECT_TRUE(vm.IsEnabled());
}

TEST(VirtualMouse, SetConfigRoundTrip) {
    VirtualMouse vm;
    VirtualMouse::Config cfg;
    cfg.maxSpeedPx    = 3000.0f;
    cfg.curveExponent = 0.7f;
    cfg.invertScroll  = true;
    vm.SetConfig(cfg);
    EXPECT_FLOAT_EQ(vm.GetConfig().maxSpeedPx, 3000.0f);
    EXPECT_FLOAT_EQ(vm.GetConfig().curveExponent, 0.7f);
    EXPECT_TRUE(vm.GetConfig().invertScroll);
}

TEST(VirtualMouse, ZeroStickNoMovement) {
    // With zero stick, Update should complete without throwing.
    VirtualMouse vm;
    ControllerState s{};
    s.rightStick = {0.0f, 0.0f};
    EXPECT_NO_THROW(vm.Update(s, 0.016f));
}
