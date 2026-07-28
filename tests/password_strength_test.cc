#include <gtest/gtest.h>

#include <QString>

#include "generator/password_strength.h"

using passvault::generator::CalculatePasswordStrength;

TEST(PasswordStrength, LongMixedFourTypesIsThree) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("Abcdef12!xyzABC")), 3);
}

TEST(PasswordStrength, TwelveCharsThreeTypesIsThree) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("Abcdefgh1234")), 3);
}

TEST(PasswordStrength, TwelveCharsTwoTypesIsTwo) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("abcdefgh1234")), 2);
}

TEST(PasswordStrength, EightCharsTwoTypesIsTwo) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("abcd1234")), 2);
}

TEST(PasswordStrength, SevenCharsTwoTypesIsOne) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("abcd123")), 1);
}

TEST(PasswordStrength, AllLowercaseTwelveCharsIsOne) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("abcdefghijkl")), 1);
}

TEST(PasswordStrength, EmptyStringIsOne) {
    EXPECT_EQ(CalculatePasswordStrength(QString()), 1);
}

TEST(PasswordStrength, SymbolsCountAsSpecial) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("aaa!!!11")), 2);
}
