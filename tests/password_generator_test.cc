#include <gtest/gtest.h>

#include <QSet>
#include <QString>

#include "generator/password_generator.h"

using passvault::generator::PasswordConfig;
using passvault::generator::PasswordGenerator;

TEST(PasswordGenerator, LengthMatchesConfig) {
    PasswordConfig cfg;
    cfg.length = 20;
    const auto out = PasswordGenerator::Generate(cfg);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->size(), 20);
}

TEST(PasswordGenerator, ZeroOrNegativeLengthReturnsNullopt) {
    PasswordConfig cfg;
    cfg.length = 0;
    EXPECT_FALSE(PasswordGenerator::Generate(cfg).has_value());
    cfg.length = -1;
    EXPECT_FALSE(PasswordGenerator::Generate(cfg).has_value());
}

TEST(PasswordGenerator, EmptyPoolReturnsNullopt) {
    PasswordConfig cfg;
    cfg.length = 8;
    cfg.include_uppercase = false;
    cfg.include_lowercase = false;
    cfg.include_numbers = false;
    cfg.include_symbols = false;
    EXPECT_FALSE(PasswordGenerator::Generate(cfg).has_value());
}

TEST(PasswordGenerator, LowercaseOnlyPoolProducesLowercase) {
    PasswordConfig cfg;
    cfg.length = 32;
    cfg.include_uppercase = false;
    cfg.include_lowercase = true;
    cfg.include_numbers = false;
    cfg.include_symbols = false;
    const auto out = PasswordGenerator::Generate(cfg);
    ASSERT_TRUE(out.has_value());
    for (const QChar ch : *out) {
        EXPECT_TRUE(ch.isLower());
    }
}

TEST(PasswordGenerator, SymbolPoolMatchesAndroid) {
    PasswordConfig cfg;
    cfg.length = 200;
    cfg.include_uppercase = false;
    cfg.include_lowercase = false;
    cfg.include_numbers = false;
    cfg.include_symbols = true;
    const auto out = PasswordGenerator::Generate(cfg);
    ASSERT_TRUE(out.has_value());
    const QString allowed = QStringLiteral("!@#$%^&*()_+-=[]{}|;:,.<>?");
    for (const QChar ch : *out) {
        EXPECT_TRUE(allowed.contains(ch)) << "unexpected char: " << ch.unicode();
    }
}

TEST(PasswordGenerator, ConsecutiveCallsProduceDifferentOutputs) {
    PasswordConfig cfg;
    cfg.length = 24;
    QSet<QString> seen;
    for (int i = 0; i < 10; ++i) {
        const auto out = PasswordGenerator::Generate(cfg);
        ASSERT_TRUE(out.has_value());
        seen.insert(*out);
    }
    EXPECT_GT(seen.size(), 8);
}
