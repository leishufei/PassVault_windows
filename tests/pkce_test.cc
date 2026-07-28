#include <gtest/gtest.h>

#include <QByteArray>
#include <QRegularExpression>
#include <QString>

#include "crypto/sha256.h"
#include "oauth/pkce.h"

using passvault::oauth::Pkce;

TEST(Pkce, GeneratesVerifierAndChallenge) {
    const auto pair = Pkce::Generate();
    EXPECT_FALSE(pair.verifier.isEmpty());
    EXPECT_FALSE(pair.challenge.isEmpty());
}

TEST(Pkce, VerifierIsBase64UrlNoPad) {
    const auto pair = Pkce::Generate();
    const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]+$"));
    EXPECT_TRUE(re.match(pair.verifier).hasMatch());
    EXPECT_FALSE(pair.verifier.contains(QLatin1Char('=')));
    EXPECT_FALSE(pair.verifier.contains(QLatin1Char('+')));
    EXPECT_FALSE(pair.verifier.contains(QLatin1Char('/')));
}

TEST(Pkce, VerifierLengthMatches32BytesBase64Url) {
    const auto pair = Pkce::Generate();
    EXPECT_EQ(pair.verifier.size(), 43);
    EXPECT_EQ(pair.challenge.size(), 43);
}

TEST(Pkce, ChallengeIsSha256OfVerifier) {
    const auto pair = Pkce::Generate();
    const QByteArray verifier_ascii = pair.verifier.toLatin1();
    const auto digest = passvault::crypto::Sha256(
        reinterpret_cast<const std::uint8_t*>(verifier_ascii.constData()),
        static_cast<std::size_t>(verifier_ascii.size()));
    const QByteArray digest_qb(reinterpret_cast<const char*>(digest.data()),
                               static_cast<int>(digest.size()));
    const QString expected = QString::fromLatin1(
        digest_qb.toBase64(QByteArray::Base64UrlEncoding |
                           QByteArray::OmitTrailingEquals));
    EXPECT_EQ(pair.challenge, expected);
}

TEST(Pkce, RandomStateShapeAndUniqueness) {
    const QString a = Pkce::RandomState();
    const QString b = Pkce::RandomState();
    EXPECT_EQ(a.size(), 43);
    EXPECT_NE(a, b);
}

TEST(Pkce, ConsecutiveVerifiersDiffer) {
    const auto p1 = Pkce::Generate();
    const auto p2 = Pkce::Generate();
    EXPECT_NE(p1.verifier, p2.verifier);
}
