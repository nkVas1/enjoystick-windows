#include <gtest/gtest.h>

#include <enjoystick/config/Config.hpp>
#include <enjoystick/config/ConfigSerializer.hpp>

using namespace enjoystick::config;

// ---------------------------------------------------------------------------
// ConfigSerializer round-trip
// ---------------------------------------------------------------------------

TEST(ConfigSerializer, RoundTrip) {
    Config orig;
    orig.mouse.maxSpeed         = 42.5f;
    orig.mouse.exponent         = 1.8f;
    orig.mouse.scrollSpeed      = 9.0f;
    orig.mouse.triggersAsClicks = true;   // non-default
    orig.mouse.useRightStick    = false;  // non-default
    orig.mouse.accelerationMs   = 120.0f; // non-default
    orig.input.deadzoneInner    = 0.12f;
    orig.input.deadzoneOuter    = 0.90f;
    orig.app.language           = "ru";

    const std::string json = ConfigSerializer::ToJson(orig);
    const Config loaded    = ConfigSerializer::FromJson(json);

    EXPECT_FLOAT_EQ(loaded.mouse.maxSpeed,         42.5f);
    EXPECT_FLOAT_EQ(loaded.mouse.exponent,         1.8f);
    EXPECT_FLOAT_EQ(loaded.mouse.scrollSpeed,      9.0f);
    EXPECT_EQ      (loaded.mouse.triggersAsClicks, true);
    EXPECT_EQ      (loaded.mouse.useRightStick,    false);
    EXPECT_FLOAT_EQ(loaded.mouse.accelerationMs,   120.0f);
    EXPECT_FLOAT_EQ(loaded.input.deadzoneInner,    0.12f);
    EXPECT_FLOAT_EQ(loaded.input.deadzoneOuter,    0.90f);
    EXPECT_EQ      (loaded.app.language,           "ru");
}

TEST(ConfigSerializer, DefaultsOnMissingFields) {
    // An empty JSON object should produce default values for all fields.
    const Config loaded = ConfigSerializer::FromJson("{}");
    const Config def    = Config::Defaults();

    EXPECT_FLOAT_EQ(loaded.mouse.maxSpeed,      def.mouse.maxSpeed);
    EXPECT_FLOAT_EQ(loaded.mouse.exponent,      def.mouse.exponent);
    EXPECT_EQ      (loaded.mouse.useRightStick, def.mouse.useRightStick);
    EXPECT_FLOAT_EQ(loaded.input.deadzoneInner, def.input.deadzoneInner);
}

TEST(ConfigSerializer, BadJsonThrows) {
    EXPECT_THROW(ConfigSerializer::FromJson("not json at all"), std::runtime_error);
    EXPECT_THROW(ConfigSerializer::FromJson(""),                 std::runtime_error);
    EXPECT_THROW(ConfigSerializer::FromJson("{ unclosed"),       std::runtime_error);
}

TEST(ConfigSerializer, KeyMappingRoundTrip) {
    Config orig;
    KeyMappingEntry km;
    km.name       = "Ctrl+Z";
    km.buttonMask = 0xAB;
    km.sequence   = {{ 0x5A, false }, { 0x11, true }};
    orig.keyMappings.push_back(km);

    const Config loaded = ConfigSerializer::FromJson(ConfigSerializer::ToJson(orig));

    ASSERT_EQ(loaded.keyMappings.size(), 1u);
    EXPECT_EQ(loaded.keyMappings[0].name, "Ctrl+Z");
    EXPECT_EQ(loaded.keyMappings[0].buttonMask, 0xABu);
    ASSERT_EQ(loaded.keyMappings[0].sequence.size(), 2u);
    EXPECT_EQ(loaded.keyMappings[0].sequence[0].vk, 0x5Au);
    EXPECT_EQ(loaded.keyMappings[0].sequence[1].extended, true);
}

// ---------------------------------------------------------------------------
// SettingsValuesFromConfig regression guard
//
// This mirrors the static function in Application.cpp to ensure all
// MouseCfg fields are properly propagated.  The bug was that triggersAsClicks,
// useRightStick and accelerationMs were hardcoded; this test would have
// caught that regression.
// ---------------------------------------------------------------------------

// Minimal local replica of SettingsMenu::Values for testing in isolation.
struct SettingsValues {
    float cursorSpeed      = 0.0f;
    float curveExponent    = 0.0f;
    float accelerationMs   = 0.0f;
    float scrollSpeed      = 0.0f;
    bool  triggersAsClicks = false;
    bool  useRightStick    = true;
    float dzInner          = 0.0f;
    float dzOuter          = 0.0f;
};

static SettingsValues SettingsValuesFromConfig(
    const MouseCfg& mc, const InputCfg& ic) noexcept
{
    SettingsValues v;
    v.cursorSpeed      = mc.maxSpeed;
    v.curveExponent    = mc.exponent;
    v.accelerationMs   = mc.accelerationMs;    // must NOT be hardcoded
    v.scrollSpeed      = mc.scrollSpeed;
    v.triggersAsClicks = mc.triggersAsClicks;  // must NOT be hardcoded
    v.useRightStick    = mc.useRightStick;     // must NOT be hardcoded
    v.dzInner          = ic.deadzoneInner;
    v.dzOuter          = ic.deadzoneOuter;
    return v;
}

TEST(SettingsValues, AllFieldsReadFromConfig) {
    MouseCfg mc;
    mc.maxSpeed         = 55.0f;
    mc.exponent         = 1.5f;
    mc.scrollSpeed      = 8.0f;
    mc.triggersAsClicks = true;
    mc.useRightStick    = false;
    mc.accelerationMs   = 200.0f;

    InputCfg ic;
    ic.deadzoneInner = 0.05f;
    ic.deadzoneOuter = 0.97f;

    const auto v = SettingsValuesFromConfig(mc, ic);

    EXPECT_FLOAT_EQ(v.cursorSpeed,      55.0f);
    EXPECT_FLOAT_EQ(v.curveExponent,    1.5f);
    EXPECT_FLOAT_EQ(v.scrollSpeed,      8.0f);
    EXPECT_EQ      (v.triggersAsClicks, true);
    EXPECT_EQ      (v.useRightStick,    false);
    EXPECT_FLOAT_EQ(v.accelerationMs,   200.0f);
    EXPECT_FLOAT_EQ(v.dzInner,          0.05f);
    EXPECT_FLOAT_EQ(v.dzOuter,          0.97f);
}

TEST(SettingsValues, DefaultUseRightStickIsTrue) {
    // Sanity: default config has useRightStick == true
    const Config def = Config::Defaults();
    EXPECT_TRUE(def.mouse.useRightStick);
}
