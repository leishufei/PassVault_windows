#include "master_password/password_change_coordinator.h"

#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSet>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "generator/password_strength.h"
#include "model/category.h"
#include "model/cloud_backup_format.h"
#include "model/password_entry.h"
#include "model/sync_payload_v2.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/statement.h"
#include "sync/cloud_crypto_service.h"
#include "sync/cloud_storage_provider.h"
#include "sync/merge_algorithm.h"
#include "sync/sync_manager.h"

namespace passvault::master_password {
namespace {

using model::Category;
using model::CategorySyncItemV2;
using model::PasswordEntry;
using model::PasswordSyncItemV2;
using model::SyncPayloadV2;
using Outcome = PasswordChangeCoordinator::Outcome;
using Result = PasswordChangeCoordinator::Result;
using Boundary = PasswordChangeCoordinator::Boundary;

constexpr char kRemoteName[] = "PassVault_Cloud_Backup.json";
enum class Phase : quint32 { kEmpty, kPrepared, kUploading, kConfirmed };

struct Snapshot {
    std::vector<PasswordEntry> passwords;
    std::vector<Category> categories;
    std::vector<storage::PasswordDeletionMarker> markers;
};

std::string_view PasswordView(const crypto::SecureBytes& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

Result Locked() {
    return {Outcome::kLocked,
            QStringLiteral("主密码迁移尚未完成，保险库保持锁定。请重试恢复；"
                           "不要删除迁移文件或重新初始化保险库。"), std::nullopt};
}

bool WriteFile(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) &&
           file.write(bytes) == bytes.size() && file.commit();
}

std::optional<QByteArray> ReadFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024 * 1024) {
        return std::nullopt;
    }
    auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return std::nullopt;
    return bytes;
}

QByteArray SerializeSnapshot(const Snapshot& snapshot) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_5);
    out << quint32(snapshot.passwords.size());
    for (const auto& p : snapshot.passwords) {
        out << qint64(p.id) << p.uuid << p.title << p.username
            << p.encrypted_password << p.password_iv << p.website
            << p.app_package_name << p.notes << p.is_favorite
            << qint32(p.icon_color) << qint32(p.strength) << qint64(p.category_id)
            << qint64(p.created_at) << qint64(p.updated_at) << p.is_deleted;
    }
    out << quint32(snapshot.categories.size());
    for (const auto& c : snapshot.categories) {
        out << qint64(c.id) << c.uuid << c.name << qint32(c.color) << c.is_default
            << qint32(c.sort_order) << qint64(c.created_at) << qint64(c.updated_at)
            << c.is_deleted;
    }
    out << quint32(snapshot.markers.size());
    for (const auto& m : snapshot.markers) out << m.uuid << qint64(m.deleted_at);
    return bytes;
}

std::optional<Snapshot> ParseSnapshot(const QByteArray& bytes) {
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_5);
    Snapshot snapshot;
    quint32 count = 0;
    in >> count;
    if (count > 1000000) return std::nullopt;
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        PasswordEntry p;
        qint64 id, category, created, updated;
        qint32 color, strength;
        in >> id >> p.uuid >> p.title >> p.username >> p.encrypted_password
           >> p.password_iv >> p.website >> p.app_package_name >> p.notes
           >> p.is_favorite >> color >> strength >> category >> created >> updated
           >> p.is_deleted;
        p.id = id;
        p.category_id = category;
        p.created_at = created;
        p.updated_at = updated;
        p.icon_color = color;
        p.strength = strength;
        snapshot.passwords.push_back(std::move(p));
    }
    in >> count;
    if (count > 1000000) return std::nullopt;
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        Category c;
        qint64 id, created, updated;
        qint32 color, order;
        in >> id >> c.uuid >> c.name >> color >> c.is_default >> order >> created
           >> updated >> c.is_deleted;
        c.id = id;
        c.created_at = created;
        c.updated_at = updated;
        c.color = color;
        c.sort_order = order;
        snapshot.categories.push_back(std::move(c));
    }
    in >> count;
    if (count > 1000000) return std::nullopt;
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        storage::PasswordDeletionMarker m;
        qint64 timestamp;
        in >> m.uuid >> timestamp;
        m.deleted_at = timestamp;
        snapshot.markers.push_back(std::move(m));
    }
    if (in.status() != QDataStream::Ok || !in.atEnd()) return std::nullopt;
    return snapshot;
}

bool IsMarker(const PasswordSyncItemV2& p) {
    return p.is_deleted && p.title.isEmpty() && p.username.isEmpty() &&
           p.password.isEmpty() && p.website.isEmpty() && p.notes.isEmpty() &&
           p.category_uuid.isEmpty() && !p.is_favorite && p.icon_color == 0 &&
           p.created_at == 0;
}

void WipePayload(SyncPayloadV2* payload) {
    for (auto& p : payload->passwords) p.password.fill(QChar(0));
}

struct PayloadWiper {
    SyncPayloadV2* payload;
    ~PayloadWiper() { WipePayload(payload); }
};

std::optional<SyncPayloadV2> ToPayload(const Snapshot& snapshot,
                                      const crypto::SessionKey& key) {
    SyncPayloadV2 payload;
    PayloadWiper wiper{&payload};
    QHash<std::int64_t, QString> category_ids;
    category_ids.insert(0, {});
    QSet<QString> uuids;
    for (const auto& c : snapshot.categories) {
        if (c.uuid.isEmpty() || uuids.contains(c.uuid)) return std::nullopt;
        uuids.insert(c.uuid);
        if (c.id != 0) category_ids.insert(c.id, c.uuid);
        payload.categories.push_back({c.uuid, c.name, c.color, c.sort_order,
                                      c.created_at, c.updated_at, c.is_deleted});
    }
    uuids.clear();
    for (const auto& p : snapshot.passwords) {
        if (p.uuid.isEmpty() || uuids.contains(p.uuid) ||
            !category_ids.contains(p.category_id)) return std::nullopt;
        uuids.insert(p.uuid);
        auto plain = crypto::CryptoService::DecryptGcm(
            key.data(), key.size(),
            reinterpret_cast<const std::uint8_t*>(p.password_iv.constData()),
            static_cast<std::size_t>(p.password_iv.size()),
            reinterpret_cast<const std::uint8_t*>(p.encrypted_password.constData()),
            static_cast<std::size_t>(p.encrypted_password.size()));
        if (!plain) return std::nullopt;
        payload.passwords.push_back({p.uuid, p.title, p.username,
                                    QString::fromUtf8(*plain), p.website, p.notes,
                                    category_ids.value(p.category_id),
                                    p.is_favorite, p.icon_color, p.created_at,
                                    p.updated_at, p.is_deleted});
        plain->fill('\0');
    }
    for (const auto& m : snapshot.markers) {
        if (m.uuid.isEmpty() || uuids.contains(m.uuid)) return std::nullopt;
        uuids.insert(m.uuid);
        PasswordSyncItemV2 item;
        item.uuid = m.uuid;
        item.updated_at = m.deleted_at;
        item.is_deleted = true;
        payload.passwords.push_back(item);
    }
    return payload;
}

bool ValidPayload(const SyncPayloadV2& payload) {
    QSet<QString> categories;
    categories.insert({});
    for (const auto& c : payload.categories) {
        if (c.uuid.isEmpty() || categories.contains(c.uuid)) return false;
        categories.insert(c.uuid);
    }
    QSet<QString> passwords;
    for (const auto& p : payload.passwords) {
        if (p.uuid.isEmpty() || passwords.contains(p.uuid) ||
            !categories.contains(p.category_uuid)) return false;
        passwords.insert(p.uuid);
    }
    return true;
}

std::optional<qint64> StrictTimestamp(const QJsonValue& value) {
    if (value.isString()) {
        bool ok = false;
        const auto number = value.toString().toLongLong(&ok);
        if (ok) return number;
    } else if (value.isDouble()) {
        const auto number = value.toInteger(std::numeric_limits<qint64>::min());
        if (number == value.toInteger(std::numeric_limits<qint64>::max())) return number;
    }
    return std::nullopt;
}

bool NormalizeTimestamp(QJsonObject* object, const char* field) {
    const auto number = StrictTimestamp(object->value(field));
    if (!number) return false;
    // The shared parser accepts strings without a lossy double-to-int64 conversion.
    object->insert(field, QString::number(*number));
    return true;
}

std::optional<SyncPayloadV2> ParseMigrationPayload(const QByteArray& bytes,
                                                 QJsonObject* original) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    auto object = document.object();
    const auto version = object.value("version");
    if (!version.isDouble() || version.toDouble() != SyncPayloadV2::kVersion ||
        !object.value("passwords").isArray() || !object.value("categories").isArray()) {
        return std::nullopt;
    }
    *original = object;
    if (object.contains("syncTimestamp") &&
        !NormalizeTimestamp(&object, "syncTimestamp")) return std::nullopt;
    for (const auto* collection : {"passwords", "categories"}) {
        const bool password = QByteArray(collection) == "passwords";
        QJsonArray normalized;
        for (const auto& value : object.value(collection).toArray()) {
            if (!value.isObject()) return std::nullopt;
            auto item = value.toObject();
            const auto strings = password
                ? std::vector<const char*>{"uuid", "title", "username", "password",
                                          "website", "notes", "categoryUuid"}
                : std::vector<const char*>{"uuid", "name"};
            for (const auto* field : strings) {
                if (!item.value(field).isString()) return std::nullopt;
            }
            if (!item.value("isDeleted").isBool() ||
                (password && !item.value("isFavorite").isBool())) return std::nullopt;
            const auto integers = password ? std::vector<const char*>{"iconColor"}
                                           : std::vector<const char*>{"color", "sortOrder"};
            for (const auto* field : integers) {
                const auto number = StrictTimestamp(item.value(field));
                if (!item.value(field).isDouble() || !number ||
                    *number < std::numeric_limits<std::int32_t>::min() ||
                    *number > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
            }
            if (!NormalizeTimestamp(&item, "createdAt") ||
                !NormalizeTimestamp(&item, "updatedAt")) return std::nullopt;
            normalized.append(item);
        }
        object.insert(collection, normalized);
    }
    auto payload = SyncPayloadV2::FromJsonObject(object);
    if (!payload || !ValidPayload(*payload) ||
        payload->passwords.size() != static_cast<std::size_t>(object.value("passwords").toArray().size()) ||
        payload->categories.size() != static_cast<std::size_t>(object.value("categories").toArray().size())) {
        if (payload) WipePayload(&*payload);
        return std::nullopt;
    }
    return payload;
}

QByteArray MigrationPayloadBytes(const SyncPayloadV2& payload, QJsonObject original) {
    auto known = payload.ToJsonObject();
    // Keep integer timestamps exact and retain opaque V2 extensions during re-encryption.
    known.insert("syncTimestamp", qint64(payload.sync_timestamp));
    for (const auto* collection : {"passwords", "categories"}) {
        QHash<QString, QJsonObject> originals;
        for (const auto& value : original.value(collection).toArray()) {
            const auto item = value.toObject();
            originals.insert(item.value("uuid").toString(), item);
        }
        QJsonArray result;
        const auto items = known.value(collection).toArray();
        for (qsizetype i = 0; i < items.size(); ++i) {
            auto fields = items.at(i).toObject();
            const bool password = QByteArray(collection) == "passwords";
            fields.insert("createdAt", qint64(password ? payload.passwords[i].created_at
                                                       : payload.categories[i].created_at));
            fields.insert("updatedAt", qint64(password ? payload.passwords[i].updated_at
                                                       : payload.categories[i].updated_at));
            auto item = originals.value(fields.value("uuid").toString());
            for (auto it = fields.begin(); it != fields.end(); ++it) item.insert(it.key(), it.value());
            result.append(item);
        }
        known.insert(collection, result);
    }
    for (auto it = known.begin(); it != known.end(); ++it) original.insert(it.key(), it.value());
    return QJsonDocument(original).toJson(QJsonDocument::Compact);
}

SyncPayloadV2 Merge(const SyncPayloadV2& local, const SyncPayloadV2& cloud,
                    const Snapshot& snapshot) {
    QHash<QString, CategorySyncItemV2> local_cats, cloud_cats;
    QHash<QString, PasswordSyncItemV2> local_passwords, cloud_passwords;
    for (const auto& c : local.categories) local_cats.insert(c.uuid, c);
    for (const auto& c : cloud.categories) cloud_cats.insert(c.uuid, c);
    for (const auto& p : local.passwords) local_passwords.insert(p.uuid, p);
    for (const auto& p : cloud.passwords) cloud_passwords.insert(p.uuid, p);
    SyncPayloadV2 merged;
    merged.categories = sync::MergeByUuid(
        local_cats, cloud_cats,
        [](const CategorySyncItemV2& c) { return c.updated_at; }).merged;
    merged.passwords = sync::MergeByUuid(
        local_passwords, cloud_passwords,
        [](const PasswordSyncItemV2& p) { return p.updated_at; }).merged;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QHash<QString, QString> winner_by_name;
    std::sort(merged.categories.begin(), merged.categories.end(),
              [](const CategorySyncItemV2& a, const CategorySyncItemV2& b) {
                  return a.created_at != b.created_at ? a.created_at < b.created_at
                                                       : a.uuid < b.uuid;
              });
    for (auto& c : merged.categories) {
        if (c.is_deleted) continue;
        const bool exempt = std::any_of(
            snapshot.categories.begin(), snapshot.categories.end(),
            [&c](const Category& old) {
                return old.uuid == c.uuid && (old.is_default || old.id == 0);
            });
        if (exempt) continue;
        const QString name = c.name.trimmed().toLower();
        if (!winner_by_name.contains(name)) {
            winner_by_name.insert(name, c.uuid);
            continue;
        }
        const QString winner = winner_by_name.value(name);
        for (auto& p : merged.passwords) {
            if (p.category_uuid == c.uuid && !p.is_deleted) {
                p.category_uuid = winner;
                p.updated_at = now;
            }
        }
        c.is_deleted = true;
        c.updated_at = now;
    }
    merged.sync_timestamp = now;
    return merged;
}

std::optional<Snapshot> EncryptCandidate(const SyncPayloadV2& payload,
                                        const Snapshot& old,
                                        const QSet<QString>& markers,
                                        const crypto::SessionKey& key) {
    Snapshot candidate;
    QHash<QString, Category> old_cats;
    QHash<QString, PasswordEntry> old_passwords;
    std::int64_t next_category = 0, next_password = 0;
    for (const auto& c : old.categories) {
        old_cats.insert(c.uuid, c);
        next_category = std::max(next_category, c.id);
    }
    for (const auto& p : old.passwords) {
        old_passwords.insert(p.uuid, p);
        next_password = std::max(next_password, p.id);
    }
    QHash<QString, std::int64_t> category_ids;
    category_ids.insert({}, 0);
    for (const auto& item : payload.categories) {
        auto c = old_cats.value(item.uuid);
        if (!old_cats.contains(item.uuid)) c.id = ++next_category;
        c.uuid = item.uuid;
        c.name = item.name;
        c.color = item.color;
        c.sort_order = item.sort_order;
        c.created_at = item.created_at;
        c.updated_at = item.updated_at;
        c.is_deleted = item.is_deleted;
        category_ids.insert(c.uuid, c.id);
        candidate.categories.push_back(std::move(c));
    }
    for (const auto& item : payload.passwords) {
        if (markers.contains(item.uuid)) {
            candidate.markers.push_back({item.uuid, item.updated_at});
            continue;
        }
        if (!category_ids.contains(item.category_uuid)) return std::nullopt;
        auto p = old_passwords.value(item.uuid);
        if (!old_passwords.contains(item.uuid)) p.id = ++next_password;
        p.uuid = item.uuid;
        p.title = item.title;
        p.username = item.username;
        p.website = item.website;
        p.notes = item.notes;
        p.is_favorite = item.is_favorite;
        p.icon_color = item.icon_color;
        if (!old_passwords.contains(item.uuid) ||
            old_passwords.value(item.uuid).updated_at < item.updated_at) {
            p.strength = generator::CalculatePasswordStrength(item.password);
        }
        p.category_id = category_ids.value(item.category_uuid);
        p.created_at = item.created_at;
        p.updated_at = item.updated_at;
        p.is_deleted = item.is_deleted;
        auto plain = item.password.toUtf8();
        const auto iv = crypto::Random::Bytes(crypto::CryptoService::kIvSize);
        p.password_iv = QByteArray(reinterpret_cast<const char*>(iv.data()), iv.size());
        p.encrypted_password = crypto::CryptoService::EncryptGcm(
            key.data(), key.size(), iv.data(), iv.size(),
            reinterpret_cast<const std::uint8_t*>(plain.constData()), plain.size());
        plain.fill('\0');
        if (static_cast<std::size_t>(p.encrypted_password.size()) < crypto::CryptoService::kTagSize) {
            return std::nullopt;
        }
        candidate.passwords.push_back(std::move(p));
    }
    return candidate;
}

bool ApplySnapshotTransaction(const Snapshot& snapshot, storage::PasswordDao& passwords,
                              storage::CategoryDao& categories) {
    auto& db = passwords.database();
    try {
        storage::Transaction tx(db);
        if (!db.Execute("DELETE FROM password_entries") ||
            !db.Execute("DELETE FROM categories") ||
            !db.Execute("DELETE FROM password_deletion_markers")) return false;
        for (const auto& c : snapshot.categories) {
            if (c.id != 0) {
                if (!categories.Insert(c)) return false;
            } else {
                storage::Statement stmt(db.handle(),
                    "INSERT INTO categories(id, uuid, name, color, isDefault, "
                    "sortOrder, createdAt, updatedAt, isDeleted) VALUES (0,?,?,?,?,?,?,?,?)");
                stmt.BindText(1, c.uuid);
                stmt.BindText(2, c.name);
                stmt.BindInt(3, c.color);
                stmt.BindBool(4, c.is_default);
                stmt.BindInt(5, c.sort_order);
                stmt.BindInt64(6, c.created_at);
                stmt.BindInt64(7, c.updated_at);
                stmt.BindBool(8, c.is_deleted);
                stmt.StepDone();
            }
        }
        for (const auto& p : snapshot.passwords) {
            if (!passwords.Insert(p)) return false;
        }
        for (const auto& m : snapshot.markers) {
            storage::Statement stmt(db.handle(),
                "INSERT INTO password_deletion_markers(uuid, deletedAt) VALUES (?, ?)");
            stmt.BindText(1, m.uuid);
            stmt.BindInt64(2, m.deleted_at);
            stmt.StepDone();
        }
        return tx.Commit();
    } catch (const std::exception&) {
        return false;
    }
}

int ReadSynchronous(storage::Database& db) {
    storage::Statement statement(db.handle(), "PRAGMA main.synchronous");
    if (!statement.Step()) throw std::runtime_error("Missing synchronous setting");
    return statement.ColumnInt(0);
}

bool ApplySnapshot(const Snapshot& snapshot, storage::PasswordDao& passwords,
                   storage::CategoryDao& categories) {
    auto& db = passwords.database();
    std::optional<int> previous;
    bool committed = false;
    try {
        if (!sqlite3_get_autocommit(db.handle())) return false;
        {
            storage::Statement mode(db.handle(), "PRAGMA main.journal_mode");
            if (!mode.Step() || mode.ColumnText(0) != "wal") return false;
        }
        previous = ReadSynchronous(db);
        if (*previous < 0 || *previous > 3) return false;
        // SQLite WAL/FULL synchronizes each successful COMMIT before returning.
        // Set and verify outside the transaction; NORMAL remains the ordinary policy.
        if (db.Execute("PRAGMA main.synchronous=FULL") && ReadSynchronous(db) == 2) {
            committed = ApplySnapshotTransaction(snapshot, passwords, categories);
        }
    } catch (const std::exception&) {
        committed = false;
    }
    if (!previous) return false;
    try {
        if (!sqlite3_get_autocommit(db.handle()) ||
            !db.Execute("PRAGMA main.synchronous=" + std::to_string(*previous)) ||
            ReadSynchronous(db) != *previous) return false;
    } catch (const std::exception&) {
        return false;
    }
    return committed;
}

}  // namespace

struct PasswordChangeCoordinator::Journal {
    Phase phase = Phase::kEmpty;
    bool cloud = false;
    QByteArray old_record;
    QByteArray new_record;
    QByteArray old_rows;
    QByteArray new_rows;
    QByteArray old_cloud;
    QByteArray new_cloud;
    QString version;
};

PasswordChangeCoordinator::PasswordChangeCoordinator(
    MasterPasswordStore& store, MasterPasswordManager& manager,
    storage::PasswordDao& passwords, storage::CategoryDao& categories,
    std::function<bool()> invalidate_hello)
    : store_(store), manager_(manager), passwords_(passwords),
      categories_(categories), invalidate_hello_(std::move(invalidate_hello)) {}

QString PasswordChangeCoordinator::journal_path() const {
    return store_.file_path() + QStringLiteral(".migration");
}

CloudBinding PasswordChangeCoordinator::ReadBinding() const {
    const auto path = store_.file_path() + QStringLiteral(".binding");
    if (!QFile::exists(path)) return CloudBinding::kUnknown;
    auto bytes = ReadFile(path);
    if (!bytes) return CloudBinding::kError;
    if (*bytes == "passvault-binding-v1:local") return CloudBinding::kLocal;
    if (*bytes == "passvault-binding-v1:bound") return CloudBinding::kBound;
    return CloudBinding::kError;
}

bool PasswordChangeCoordinator::SaveBinding(CloudBinding binding) const {
    if (binding != CloudBinding::kLocal && binding != CloudBinding::kBound) return false;
    // A known cloud history cannot be downgraded by signing out.
    if (ReadBinding() == CloudBinding::kBound && binding != CloudBinding::kBound) {
        return false;
    }
    return WriteFile(store_.file_path() + QStringLiteral(".binding"),
                     binding == CloudBinding::kLocal ? "passvault-binding-v1:local"
                                                     : "passvault-binding-v1:bound");
}

bool PasswordChangeCoordinator::Save(const Journal& journal) const {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_5);
    out << quint32(1) << quint32(journal.phase) << journal.cloud
        << journal.old_record << journal.new_record << journal.old_rows
        << journal.new_rows << journal.old_cloud << journal.new_cloud
        << journal.version;
    auto blob = MasterPasswordStore::Protect(bytes);
    bytes.fill('\0');
    return blob && WriteFile(journal_path(), *blob);
}

std::optional<PasswordChangeCoordinator::Journal>
PasswordChangeCoordinator::Load() const {
    if (!QFile::exists(journal_path())) return Journal{};
    auto blob = ReadFile(journal_path());
    if (!blob) return std::nullopt;
    auto bytes = MasterPasswordStore::Unprotect(*blob);
    if (!bytes) return std::nullopt;
    QDataStream in(*bytes);
    in.setVersion(QDataStream::Qt_6_5);
    quint32 version = 0, phase = 0;
    Journal journal;
    in >> version >> phase >> journal.cloud >> journal.old_record
       >> journal.new_record >> journal.old_rows >> journal.new_rows
       >> journal.old_cloud >> journal.new_cloud >> journal.version;
    const bool valid = version == 1 && phase <= quint32(Phase::kConfirmed) &&
                       in.status() == QDataStream::Ok && in.atEnd();
    bytes->fill('\0');
    if (!valid) return std::nullopt;
    journal.phase = Phase(phase);
    if (journal.phase != Phase::kEmpty &&
        (journal.old_record.isEmpty() || journal.new_record.isEmpty() ||
         !ParseSnapshot(journal.old_rows) || !ParseSnapshot(journal.new_rows) ||
         (journal.cloud && (journal.old_cloud.isEmpty() || journal.new_cloud.isEmpty() ||
                            journal.version.isEmpty())))) return std::nullopt;
    return journal;
}

bool PasswordChangeCoordinator::NeedsRecovery() const {
    auto journal = Load();
    return !journal || journal->phase != Phase::kEmpty;
}

bool PasswordChangeCoordinator::FinishJournal() const { return Save(Journal{}); }

bool PasswordChangeCoordinator::Check(Boundary boundary) const {
    return !checkpoint_ || checkpoint_(boundary);
}

PasswordChangeCoordinator::Result PasswordChangeCoordinator::Commit(Journal& journal) {
    journal.phase = Phase::kConfirmed;
    if (!Save(journal) || !Check(Boundary::kCloudConfirmed)) return Locked();
    auto snapshot = ParseSnapshot(journal.new_rows);
    if (!snapshot || !Check(Boundary::kBeforeDatabase) ||
        !ApplySnapshot(*snapshot, passwords_, categories_) ||
        !Check(Boundary::kAfterDatabase) || !Check(Boundary::kBeforeRecord) ||
        !store_.SaveRaw(journal.new_record) || !Check(Boundary::kAfterRecord) ||
        !invalidate_hello_ || !invalidate_hello_() ||
        !Check(Boundary::kBeforeFinish) || !FinishJournal()) return Locked();
    return {Outcome::kCompleted, QStringLiteral("主密码已更新。"), std::nullopt};
}

PasswordChangeCoordinator::Result PasswordChangeCoordinator::ResolveUpload(
    Journal& journal, sync::CloudStorageProvider* provider) {
    if (!provider || !provider->IsAuthenticated()) return Locked();
    QString error;
    const auto read = provider->ReadBackup(QString::fromLatin1(kRemoteName), &error);
    if (read.status != sync::CloudReadStatus::kFound) return Locked();
    if (read.backup.data == journal.new_cloud) return Commit(journal);
    if (read.backup.data == journal.old_cloud && read.backup.version == journal.version) {
        if (!FinishJournal()) return Locked();
        return {Outcome::kUnchanged, QStringLiteral("云端尚未改变，仍使用原主密码。"),
                std::nullopt};
    }
    return Locked();
}

PasswordChangeCoordinator::Result PasswordChangeCoordinator::Recover(
    sync::CloudStorageProvider* provider) {
    if (running_) return Locked();
    QScopedValueRollback<bool> guard(running_, true);
    auto journal = Load();
    if (!journal) return Locked();
    if (journal->phase == Phase::kEmpty) return {};
    if (journal->phase == Phase::kConfirmed) return Commit(*journal);
    if (journal->phase == Phase::kUploading) return ResolveUpload(*journal, provider);
    if (!FinishJournal()) return Locked();
    return {Outcome::kUnchanged, QStringLiteral("未开始提交，仍使用原主密码。"), std::nullopt};
}

PasswordChangeCoordinator::Result PasswordChangeCoordinator::Change(
    const QString& old_password, const QString& new_password,
    const QString& confirmation, sync::CloudStorageProvider* provider) {
    if (running_ || NeedsRecovery()) return Locked();
    QScopedValueRollback<bool> guard(running_, true);
    if (new_password != confirmation) {
        return {Outcome::kUnchanged, QStringLiteral("两次输入不一致。"), std::nullopt};
    }
    const auto binding = ReadBinding();
    if (binding == CloudBinding::kUnknown || binding == CloudBinding::kError) {
        return {Outcome::kUnchanged, QStringLiteral("请先确认保险库的云端绑定历史。"),
                std::nullopt};
    }
    const bool cloud = binding == CloudBinding::kBound;
    if (cloud && (!provider || !provider->IsAuthenticated())) {
        return {Outcome::kUnchanged, QStringLiteral("此保险库曾绑定云端，请先重新登录。"),
                std::nullopt};
    }
    try {
        Journal journal;
        auto change = manager_.BuildChange(old_password, new_password, &journal.new_record);
        if (!change.success) return {Outcome::kUnchanged, change.message, std::nullopt};
        auto old_bytes = old_password.toUtf8();
        auto old = manager_.VerifyLocal(std::string_view(old_bytes.constData(), old_bytes.size()));
        old_bytes.fill('\0');
        auto old_record = store_.LoadRaw();
        if (!old || !old_record) {
            return {Outcome::kUnchanged, QStringLiteral("读取主密码记录失败。"), std::nullopt};
        }
        std::optional<Snapshot> snapshot;
        {
            storage::Transaction tx(passwords_.database());
            auto passwords = passwords_.ReadAll();
            auto categories = categories_.ReadAll();
            auto markers = passwords_.ListDeletionMarkers();
            if (!passwords || !categories || !markers || !tx.Commit()) {
                return {Outcome::kUnchanged, QStringLiteral("读取保险库失败，未修改主密码。"),
                        std::nullopt};
            }
            snapshot = Snapshot{*passwords, *categories, *markers};
        }
        auto local = ToPayload(*snapshot, old->session_key);
        if (!local) {
            return {Outcome::kUnchanged, QStringLiteral("存在损坏或无法解密的条目，未修改主密码。"),
                    std::nullopt};
        }
        PayloadWiper local_wiper{&*local};
        journal.old_record = *old_record;
        journal.old_rows = SerializeSnapshot(*snapshot);
        journal.cloud = cloud;
        for (int attempt = 0; attempt < sync::SyncManager::kDefaultMaxRetries; ++attempt) {
            SyncPayloadV2 merged = *local;
            QJsonObject original_cloud;
            PayloadWiper merged_wiper{&merged};
            QSet<QString> selected_markers;
            for (const auto& marker : snapshot->markers) selected_markers.insert(marker.uuid);
            if (cloud) {
                QString error;
                auto read = provider->ReadBackup(QString::fromLatin1(kRemoteName), &error);
                if (read.status != sync::CloudReadStatus::kFound) {
                    return {Outcome::kUnchanged,
                            read.status == sync::CloudReadStatus::kMissing
                                ? QStringLiteral("云端备份不存在，请先完成首次同步。")
                                : QStringLiteral("无法核对云端备份，请重新登录或稍后重试。"),
                            std::nullopt};
                }
                if (read.backup.version.isEmpty()) return Locked();
                auto backup = model::CloudBackupFormat::FromJsonBytes(read.backup.data);
                auto plain = backup ? sync::CloudCryptoService::DecryptFromCloud(
                    *backup, PasswordView(old->master_password)) : std::nullopt;
                auto payload = plain ? ParseMigrationPayload(*plain, &original_cloud) : std::nullopt;
                if (plain) plain->fill('\0');
                if (!payload || !ValidPayload(*payload)) {
                    return {Outcome::kUnchanged, QStringLiteral("云端备份损坏或主密码不匹配。"),
                            std::nullopt};
                }
                PayloadWiper cloud_wiper{&*payload};
                QHash<QString, std::int64_t> local_times;
                for (const auto& p : local->passwords) local_times.insert(p.uuid, p.updated_at);
                for (const auto& p : payload->passwords) {
                    if (!local_times.contains(p.uuid) || local_times.value(p.uuid) < p.updated_at) {
                        if (IsMarker(p)) selected_markers.insert(p.uuid);
                        else selected_markers.remove(p.uuid);
                    }
                }
                merged = Merge(*local, *payload, *snapshot);
                journal.old_cloud = read.backup.data;
                journal.version = read.backup.version;
            }
            auto candidate = EncryptCandidate(merged, *snapshot, selected_markers, change.payload->session_key);
            if (!candidate) return Locked();
            journal.new_rows = SerializeSnapshot(*candidate);
            if (cloud) {
                auto bytes = MigrationPayloadBytes(merged, original_cloud);
                auto backup = sync::CloudCryptoService::EncryptForCloud(
                    bytes, PasswordView(change.payload->master_password));
                bytes.fill('\0');
                if (!backup) return Locked();
                journal.new_cloud = backup->ToJsonBytes();
            }
            journal.phase = Phase::kPrepared;
            if (!Save(journal)) return {Outcome::kUnchanged,
                QStringLiteral("无法保存迁移快照，未修改主密码。"), std::nullopt};
            if (!Check(Boundary::kPrepared)) return Locked();
            Result result;
            if (cloud) {
                journal.phase = Phase::kUploading;
                if (!Save(journal) || !Check(Boundary::kBeforeUpload)) return Locked();
                QString error;
                const auto uploaded = provider->UploadBackupIfMatch(
                    journal.new_cloud, QString::fromLatin1(kRemoteName), journal.version, &error);
                if (!Check(Boundary::kAfterUpload)) return Locked();
                if (uploaded == sync::UploadIfMatchStatus::kVersionMismatch) {
                    if (!FinishJournal()) return Locked();
                    continue;
                }
                // Even an HTTP success is verified against the persisted candidate.
                result = ResolveUpload(journal, provider);
            } else {
                result = Commit(journal);
            }
            if (result.outcome == Outcome::kCompleted) result.payload = std::move(change.payload);
            return result;
        }
        return {Outcome::kUnchanged, QStringLiteral("多次重试后仍存在并发冲突，未修改主密码。"),
                std::nullopt};
    } catch (const std::exception&) {
        if (NeedsRecovery()) return Locked();
        return {Outcome::kUnchanged, QStringLiteral("准备迁移失败，未修改主密码。"), std::nullopt};
    }
}

}  // namespace passvault::master_password
