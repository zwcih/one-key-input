#include <gtest/gtest.h>

#include "inject/InjectorStrategy.h"
#include "config/Config.h"

#include <stdexcept>

using namespace onekey::inject;
using onekey::config::InjectConfig;

TEST(InjectorStrategy, SendInputModeMapsToSendInputPrimary) {
    InjectConfig cfg;
    cfg.mode = "sendinput";
    InjectorStrategy s(cfg);
    EXPECT_EQ(s.PrimaryName(), "sendinput");
}

TEST(InjectorStrategy, ClipboardModeMapsToClipboardPrimary) {
    InjectConfig cfg;
    cfg.mode = "clipboard";
    InjectorStrategy s(cfg);
    EXPECT_EQ(s.PrimaryName(), "clipboard");
}

TEST(InjectorStrategy, AutoModeMapsToSendInputPrimary) {
    InjectConfig cfg;
    cfg.mode = "auto";
    InjectorStrategy s(cfg);
    EXPECT_EQ(s.PrimaryName(), "sendinput");
}

TEST(InjectorStrategy, EmptyModeMapsToSendInputPrimary) {
    InjectConfig cfg;
    cfg.mode.clear();
    InjectorStrategy s(cfg);
    EXPECT_EQ(s.PrimaryName(), "sendinput");
}

TEST(InjectorStrategy, UnknownModeThrows) {
    InjectConfig cfg;
    cfg.mode = "carrier-pigeon";
    EXPECT_THROW({ InjectorStrategy s(cfg); }, std::runtime_error);
}
