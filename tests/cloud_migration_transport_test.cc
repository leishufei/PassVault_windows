#include <gtest/gtest.h>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>

#include <cstring>
#include <deque>
#include <vector>

#include "oauth/google_oauth_client.h"
#include "oauth/token_store.h"
#include "sync/google_drive_provider.h"

namespace {

struct Response {
    QByteArray body;
    int status = 200;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};

class ScriptedReply : public QNetworkReply {
 public:
    ScriptedReply(const QNetworkRequest& request, Response response, QObject* parent)
        : QNetworkReply(parent), body_(std::move(response.body)) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        if (response.error != NoError) setError(response.error, "injected transport failure");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this]() {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override {}
    qint64 bytesAvailable() const override {
        return body_.size() - offset_ + QNetworkReply::bytesAvailable();
    }

 protected:
    qint64 readData(char* data, qint64 maximum) override {
        const auto count = std::min(maximum, qint64(body_.size() - offset_));
        if (count <= 0) return -1;
        std::memcpy(data, body_.constData() + offset_, count);
        offset_ += count;
        return count;
    }

 private:
    QByteArray body_;
    qint64 offset_ = 0;
};

class ScriptedNetwork : public QNetworkAccessManager {
 public:
    std::deque<Response> responses;
    std::vector<QUrl> urls;
    std::vector<Operation> operations;

 protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice*) override {
        urls.push_back(request.url());
        operations.push_back(operation);
        if (responses.empty()) {
            ADD_FAILURE() << "Unexpected isolated request";
            return new ScriptedReply(request, {{}, 500, QNetworkReply::UnknownNetworkError}, this);
        }
        auto response = responses.front();
        responses.pop_front();
        return new ScriptedReply(request, std::move(response), this);
    }
};

class CloudMigrationTransportTest : public testing::Test {
 protected:
    CloudMigrationTransportTest() : provider_([]() { return QStringLiteral("isolated-access"); }) {
        provider_.set_network_manager(&network_);
    }
    void FolderAndFile() {
        network_.responses.push_back({R"({"files":[{"id":"isolated-folder"}]})"});
        network_.responses.push_back({R"({"files":[{"id":"isolated-file"}]})"});
    }
    ScriptedNetwork network_;
    passvault::sync::GoogleDriveProvider provider_;
};

TEST_F(CloudMigrationTransportTest, MissingFolderIsReadOnlyAndExplicitlyMissing) {
    network_.responses.push_back({R"({"files":[]})"});
    QString error;
    const auto result = provider_.ReadBackup("backup", &error);
    EXPECT_EQ(result.status, passvault::sync::CloudReadStatus::kMissing);
    ASSERT_EQ(network_.operations.size(), 1);
    EXPECT_EQ(network_.operations.front(), QNetworkAccessManager::GetOperation);
}

TEST_F(CloudMigrationTransportTest, InvalidListAuthenticationAndTimeoutAreNeverMissing) {
    for (const auto& response : std::vector<Response>{
            {"invalid json"}, {R"({"notFiles":[]})"},
            {{}, 401, QNetworkReply::AuthenticationRequiredError},
            {{}, 0, QNetworkReply::TimeoutError},
            {R"({"files":[]})", 200, QNetworkReply::RemoteHostClosedError}}) {
        network_.responses.push_back(response);
        QString error;
        EXPECT_EQ(provider_.ReadBackup("backup", &error).status,
                  passvault::sync::CloudReadStatus::kError);
        EXPECT_FALSE(error.isEmpty());
    }
}

TEST_F(CloudMigrationTransportTest, MalformedFileListDoesNotBecomeMissingBackup) {
    network_.responses.push_back({R"({"files":[{"id":"folder"}]})"});
    network_.responses.push_back({"not json"});
    QString error;
    EXPECT_EQ(provider_.ReadBackup("backup", &error).status,
              passvault::sync::CloudReadStatus::kError);
}

TEST_F(CloudMigrationTransportTest, BodyIsReturnedOnlyWithStableFullModifiedTime) {
    FolderAndFile();
    network_.responses.push_back({R"({"modifiedTime":"2026-10-27T01:02:03.123456789Z"})"});
    network_.responses.push_back({"isolated encrypted backup"});
    network_.responses.push_back({R"({"modifiedTime":"2026-10-27T01:02:03.123456789Z"})"});
    QString error;
    auto result = provider_.ReadBackup("backup", &error);
    ASSERT_EQ(result.status, passvault::sync::CloudReadStatus::kFound);
    EXPECT_EQ(result.backup.data, "isolated encrypted backup");
    EXPECT_EQ(result.backup.version, "2026-10-27T01:02:03.123456789Z");
    EXPECT_TRUE(network_.responses.empty());
    FolderAndFile();
    network_.responses.push_back({R"({"modifiedTime":"2026-10-27T01:02:03.123456789Z"})"});
    network_.responses.push_back({"isolated different backup"});
    network_.responses.push_back({R"({"modifiedTime":"2026-10-27T01:02:03.123456790Z"})"});
    EXPECT_EQ(provider_.ReadBackup("backup", &error).status,
              passvault::sync::CloudReadStatus::kError);
}

TEST_F(CloudMigrationTransportTest, ExpectedVersionDisappearsWithoutCreatingReplacement) {
    network_.responses.push_back({R"({"files":[{"id":"folder"}]})"});
    network_.responses.push_back({R"({"files":[]})"});
    QString error;
    EXPECT_EQ(provider_.UploadBackupIfMatch("candidate", "backup",
        "2026-10-27T01:02:03.123456789Z", &error),
        passvault::sync::UploadIfMatchStatus::kVersionMismatch);
    ASSERT_EQ(network_.operations.size(), 2);
    for (const auto operation : network_.operations) {
        EXPECT_EQ(operation, QNetworkAccessManager::GetOperation);
    }
}

TEST_F(CloudMigrationTransportTest, SubMillisecondVersionDifferenceRefusesUpload) {
    FolderAndFile();
    network_.responses.push_back({R"({"modifiedTime":"2026-10-27T01:02:03.123456790Z"})"});
    QString error;
    EXPECT_EQ(provider_.UploadBackupIfMatch("candidate", "backup",
        "2026-10-27T01:02:03.123456789Z", &error),
        passvault::sync::UploadIfMatchStatus::kVersionMismatch);
    EXPECT_EQ(network_.operations.size(), 3);
}

TEST(PasswordChangeOAuthTest, RestartRefreshesPersistedTokenAndCachesOnlyValidAccessToken) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    passvault::oauth::TokenStore store(dir.filePath("isolated-token.dat"));
    ASSERT_TRUE(store.SaveRefreshToken("isolated-refresh"));
    ScriptedNetwork network;
    network.responses.push_back({R"({"access_token":"isolated-access","expires_in":3600})"});
    {
        passvault::oauth::GoogleOAuthClient client(&store);
        client.set_network_manager(&network);
        EXPECT_FALSE(client.HasAccessToken());
        EXPECT_EQ(client.EnsureAccessToken(), "isolated-access");
        EXPECT_EQ(client.EnsureAccessToken(), "isolated-access");
        EXPECT_EQ(network.operations.size(), 1);
    }
    network.responses.push_back({R"({"access_token":"isolated-second","expires_in":3600})"});
    passvault::oauth::GoogleOAuthClient restarted(&store);
    restarted.set_network_manager(&network);
    EXPECT_EQ(restarted.EnsureAccessToken(), "isolated-second");
    EXPECT_EQ(network.operations.size(), 2);
}

TEST(PasswordChangeOAuthTest, FailedRefreshCannotSupplyAnEmptyInMemoryTokenAsSuccess) {
    QTemporaryDir dir;
    passvault::oauth::TokenStore store(dir.filePath("isolated-token.dat"));
    ASSERT_TRUE(store.SaveRefreshToken("isolated-refresh"));
    ScriptedNetwork network;
    network.responses.push_back({{}, 401, QNetworkReply::AuthenticationRequiredError});
    passvault::oauth::GoogleOAuthClient client(&store);
    client.set_network_manager(&network);
    EXPECT_TRUE(client.EnsureAccessToken().isEmpty());
    EXPECT_FALSE(client.HasAccessToken());
    EXPECT_EQ(network.operations.size(), 1);
}

}  // namespace

TEST_F(CloudMigrationTransportTest, ExpectedFolderDisappearsWithoutCreatingReplacement) {
    network_.responses.push_back({R"({"files":[]})"});
    QString error;
    EXPECT_EQ(provider_.UploadBackupIfMatch("candidate", "backup", "expected-full-version", &error),
              passvault::sync::UploadIfMatchStatus::kVersionMismatch);
    ASSERT_EQ(network_.operations.size(), 1);
    EXPECT_EQ(network_.operations.front(), QNetworkAccessManager::GetOperation);
}

namespace {

using AccountStatus = passvault::oauth::GoogleOAuthClient::AccountStatus;

class GoogleAccountLookupTest : public testing::Test {
 protected:
    GoogleAccountLookupTest() : store_(directory_.filePath("account-token.dat")),
                               client_(&store_) {
        client_.set_network_manager(&network_);
    }
    void SetUp() override {
        ASSERT_TRUE(directory_.isValid());
        ASSERT_TRUE(store_.SaveRefreshToken("isolated-refresh"));
    }
    void QueueToken() {
        network_.responses.push_back({R"({"access_token":"isolated-access","expires_in":3600})"});
    }
    void WaitFor(QSignalSpy& result) {
        for (int i = 0; i < 100 && result.isEmpty(); ++i) QTest::qWait(1);
        ASSERT_EQ(result.count(), 1);
    }
    QTemporaryDir directory_;
    passvault::oauth::TokenStore store_;
    ScriptedNetwork network_;
    passvault::oauth::GoogleOAuthClient client_;
};

TEST_F(GoogleAccountLookupTest, RestartLoadsCredentialAndQueriesEmailWithoutBackupAccess) {
    QueueToken();
    network_.responses.push_back({R"({"user":{"emailAddress":"current@example.invalid"}})"});
    QSignalSpy result(&client_, &passvault::oauth::GoogleOAuthClient::AccountLookupFinished);
    client_.LookupAccount();
    EXPECT_TRUE(result.isEmpty());
    WaitFor(result);
    ASSERT_EQ(result.count(), 1);
    EXPECT_EQ(result.first().first().value<AccountStatus>(), AccountStatus::kConnected);
    EXPECT_EQ(result.first().at(1).toString(), "current@example.invalid");
    ASSERT_EQ(network_.urls.size(), 2);
    EXPECT_EQ(network_.urls.back().path(), "/drive/v3/about");
    EXPECT_EQ(network_.operations.back(), QNetworkAccessManager::GetOperation);
}

TEST_F(GoogleAccountLookupTest, NetworkFailurePreservesCredentialAndCanRetry) {
    network_.responses.push_back({{}, 0, QNetworkReply::TimeoutError});
    QSignalSpy result(&client_, &passvault::oauth::GoogleOAuthClient::AccountLookupFinished);
    client_.LookupAccount();
    WaitFor(result);
    ASSERT_EQ(result.count(), 1);
    EXPECT_EQ(result.first().first().value<AccountStatus>(), AccountStatus::kNetworkError);
    EXPECT_EQ(store_.LoadRefreshToken().value_or(QString()), "isolated-refresh");
    result.clear();
    QueueToken();
    network_.responses.push_back({R"({"user":{"emailAddress":"retry@example.invalid"}})"});
    client_.LookupAccount();
    WaitFor(result);
    ASSERT_EQ(result.count(), 1);
    EXPECT_EQ(result.first().at(1).toString(), "retry@example.invalid");
}

TEST_F(GoogleAccountLookupTest, ExplicitInvalidGrantRequiresAuthorization) {
    network_.responses.push_back({R"({"error":"invalid_grant"})", 400,
                                  QNetworkReply::ContentOperationNotPermittedError});
    QSignalSpy result(&client_, &passvault::oauth::GoogleOAuthClient::AccountLookupFinished);
    client_.LookupAccount();
    WaitFor(result);
    ASSERT_EQ(result.count(), 1);
    EXPECT_EQ(result.first().first().value<AccountStatus>(), AccountStatus::kNeedsAuthorization);
    EXPECT_EQ(network_.urls.size(), 1);
}

TEST_F(GoogleAccountLookupTest, SuccessfulResponseWithoutEmailIsExplicit) {
    QueueToken();
    network_.responses.push_back({R"({"user":{}})"});
    QSignalSpy result(&client_, &passvault::oauth::GoogleOAuthClient::AccountLookupFinished);
    client_.LookupAccount();
    WaitFor(result);
    ASSERT_EQ(result.count(), 1);
    EXPECT_EQ(result.first().first().value<AccountStatus>(), AccountStatus::kMissingEmail);
    EXPECT_TRUE(result.first().at(1).toString().isEmpty());
}

TEST_F(GoogleAccountLookupTest, CancelDuringRefreshIgnoresLateResponse) {
    QueueToken();
    QSignalSpy result(&client_, &passvault::oauth::GoogleOAuthClient::AccountLookupFinished);
    client_.LookupAccount();
    client_.CancelAccountLookup();
    QTest::qWait(10);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(network_.urls.size(), 1);
    EXPECT_FALSE(client_.HasAccessToken());
}

TEST_F(GoogleAccountLookupTest, ReplacingAccountQueryIgnoresOldEmail) {
    QueueToken();
    ASSERT_EQ(client_.EnsureAccessToken(), "isolated-access");
    network_.responses.push_back({R"({"user":{"emailAddress":"old@example.invalid"}})"});
    network_.responses.push_back({R"({"user":{"emailAddress":"new@example.invalid"}})"});
    QSignalSpy result(&client_, &passvault::oauth::GoogleOAuthClient::AccountLookupFinished);
    client_.LookupAccount();
    client_.CancelAccountLookup();
    client_.LookupAccount();
    WaitFor(result);
    ASSERT_EQ(result.count(), 1);
    EXPECT_EQ(result.first().at(1).toString(), "new@example.invalid");
}

TEST_F(GoogleAccountLookupTest, CancelDuringEmailQueryIgnoresLateResponse) {
    QueueToken();
    ASSERT_EQ(client_.EnsureAccessToken(), "isolated-access");
    network_.responses.push_back({R"({"user":{"emailAddress":"old@example.invalid"}})"});
    QSignalSpy result(&client_, &passvault::oauth::GoogleOAuthClient::AccountLookupFinished);
    client_.LookupAccount();
    client_.CancelAccountLookup();
    QTest::qWait(10);
    EXPECT_TRUE(result.isEmpty());
}

}  // namespace
