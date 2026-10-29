#pragma once

#include <QString>

#include <optional>

namespace passvault::oauth {

class TokenStore {
 public:
    TokenStore();
    explicit TokenStore(QString file_path);

    const QString& file_path() const { return file_path_; }

    bool Exists() const;
    bool SaveRefreshToken(const QString& refresh_token) const;
    std::optional<QString> LoadRefreshToken() const;
    bool Clear() const;

    static QString DefaultFilePath();

 private:

    QString file_path_;
};

}  // namespace passvault::oauth
