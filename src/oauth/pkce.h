#pragma once

#include <QByteArray>
#include <QString>

namespace passvault::oauth {

struct PkcePair {
    QString verifier;
    QString challenge;
};

class Pkce {
 public:
    static PkcePair Generate();
    static QString RandomState();

 private:
    static QString Base64UrlNoPad(const QByteArray& bytes);
};

}  // namespace passvault::oauth
