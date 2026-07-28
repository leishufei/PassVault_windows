#include "oauth/pkce.h"

#include <QByteArray>

#include "crypto/random.h"
#include "crypto/sha256.h"

namespace passvault::oauth {

QString Pkce::Base64UrlNoPad(const QByteArray& bytes) {
    QByteArray b64 = bytes.toBase64(QByteArray::Base64UrlEncoding |
                                    QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(b64);
}

PkcePair Pkce::Generate() {
    const auto verifier_bytes = crypto::Random::Bytes(32);
    const QByteArray verifier_qb(
        reinterpret_cast<const char*>(verifier_bytes.data()),
        static_cast<int>(verifier_bytes.size()));
    const QString verifier = Base64UrlNoPad(verifier_qb);

    const QByteArray verifier_ascii = verifier.toLatin1();
    const auto digest = crypto::Sha256(
        reinterpret_cast<const std::uint8_t*>(verifier_ascii.constData()),
        static_cast<std::size_t>(verifier_ascii.size()));
    const QByteArray digest_qb(reinterpret_cast<const char*>(digest.data()),
                               static_cast<int>(digest.size()));
    const QString challenge = Base64UrlNoPad(digest_qb);

    return {verifier, challenge};
}

QString Pkce::RandomState() {
    const auto bytes = crypto::Random::Bytes(32);
    const QByteArray qb(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<int>(bytes.size()));
    return Base64UrlNoPad(qb);
}

}  // namespace passvault::oauth
