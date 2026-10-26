#include <gtest/gtest.h>

#include <QString>

#include "generator/password_strength.h"

using passvault::generator::CalculatePasswordStrength;

TEST(PasswordStrength, LongMixedFourTypesIsThree) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("mQ7!vR2#xT9@pL4$")), 3);
}

TEST(PasswordStrength, TwelveVariedCharactersAreModerate) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("mQ7vR2xT9pL4")), 2);
}

TEST(PasswordStrength, MixedConsecutiveRunsAreWeak) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("Abcdefgh1234")), 1);
}

TEST(PasswordStrength, TenVariedLettersAndDigitsAreModerate) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("m7q2v9r4t8")), 2);
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

TEST(PasswordStrength, LongVariedSymbolsCanBeStrong) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("!{?%]-@#^&=+;:*/~")), 3);
}

TEST(PasswordStrength, LongVariedSingleCaseCanBeStrong) {
    EXPECT_EQ(CalculatePasswordStrength(
        QStringLiteral("qmxvtrplkznghjfswdub")), 3);
}

TEST(PasswordStrength, RepeatedCharactersRemainWeak) {
    EXPECT_EQ(CalculatePasswordStrength(QString(80, QLatin1Char('!'))), 1);
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("aaa!!!11")), 1);
}

TEST(PasswordStrength, RepeatedFragmentsIncludingPartialRepetitionRemainWeak) {
    EXPECT_EQ(CalculatePasswordStrength(
        QStringLiteral("aB3!aB3!aB3!aB3!")), 1);
    EXPECT_EQ(CalculatePasswordStrength(
        QStringLiteral("aB3!aB3!aB3!aB")), 1);
}

TEST(PasswordStrength, AscendingAndDescendingRunsRemainWeak) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("abcdefghijklmnop")), 1);
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("9876543210")), 1);
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("!\"#$%&'()*+,-./")), 1);
}

TEST(PasswordStrength, LowDiversityAndWhitespaceAreWeak) {
    EXPECT_EQ(CalculatePasswordStrength(QStringLiteral("ababbabaababbaba")), 1);
    EXPECT_EQ(CalculatePasswordStrength(QString(40, QLatin1Char(' '))), 1);
}
