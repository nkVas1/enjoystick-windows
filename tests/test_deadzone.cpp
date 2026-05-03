#include <gtest/gtest.h>
#include <enjoystick/core/DeadzoneFilter.hpp>

using namespace enjoystick;
using namespace enjoystick::core;

TEST(DeadzoneFilter, NonePassThrough) {
    DeadzoneFilter f({DeadzoneConfig::Mode::None, 0.1f, 0.95f});
    const Vec2 in{0.5f, 0.3f};
    const Vec2 out = f.Apply(in);
    EXPECT_FLOAT_EQ(out.x, in.x);
    EXPECT_FLOAT_EQ(out.y, in.y);
}

TEST(DeadzoneFilter, RadialInnerDeadzone) {
    DeadzoneFilter f({DeadzoneConfig::Mode::ScaledRadial, 0.2f, 0.95f});
    // Magnitude = 0.1 < 0.2 → should return zero
    const Vec2 out = f.Apply({0.07f, 0.07f});
    EXPECT_FLOAT_EQ(out.x, 0.0f);
    EXPECT_FLOAT_EQ(out.y, 0.0f);
}

TEST(DeadzoneFilter, RadialOuterClamp) {
    DeadzoneFilter f({DeadzoneConfig::Mode::ScaledRadial, 0.08f, 0.95f});
    // Full deflection: magnitude 1.0 > outer 0.95 → should return unit vector
    const Vec2 out = f.Apply({1.0f, 0.0f});
    EXPECT_NEAR(out.x, 1.0f, 0.001f);
    EXPECT_FLOAT_EQ(out.y, 0.0f);
}

TEST(DeadzoneFilter, ScaledRadialRescalesLiveZone) {
    // At 50% of live zone, should return ~0.5 magnitude
    DeadzoneFilter f({DeadzoneConfig::Mode::ScaledRadial, 0.0f, 1.0f});
    const Vec2 out = f.Apply({0.5f, 0.0f});
    EXPECT_NEAR(out.x, 0.5f, 0.01f);
}

TEST(DeadzoneFilter, AxialIndependentAxes) {
    DeadzoneFilter f({DeadzoneConfig::Mode::Axial, 0.15f, 0.98f});
    const Vec2 out = f.Apply({0.1f, 0.5f}); // x below threshold, y above
    EXPECT_FLOAT_EQ(out.x, 0.0f);
    EXPECT_FLOAT_EQ(out.y, 0.5f);
}

TEST(DeadzoneFilter, DirectionPreserved) {
    DeadzoneFilter f({DeadzoneConfig::Mode::ScaledRadial, 0.05f, 0.98f});
    const Vec2 in{0.6f, 0.8f}; // direction: ~36.87 degrees
    const Vec2 out = f.Apply(in);
    // Direction ratio y/x should remain ~0.8/0.6 = 4/3
    ASSERT_GT(std::abs(out.x), 1e-5f);
    EXPECT_NEAR(out.y / out.x, 0.8f / 0.6f, 0.01f);
}
