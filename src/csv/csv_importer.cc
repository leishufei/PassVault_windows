#include "csv/csv_importer.h"

#include <QHash>
#include <QRegularExpression>
#include <QUuid>

#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "crypto/session_key.h"
#include "csv/csv_validator.h"
#include "generator/password_strength.h"
#include "model/category.h"
#include "model/password_entry.h"
#include "storage/category_dao.h"
#include "storage/password_dao.h"

namespace passvault::csv {

namespace {

QString EnsureUuid(const QString& maybe_uuid) {
    static const QRegularExpression kRe(
        QStringLiteral(
            "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"),
        QRegularExpression::CaseInsensitiveOption);
    if (kRe.match(maybe_uuid).hasMatch()) return maybe_uuid.toLower();
    return QUuid::createUuid()
        .toString(QUuid::WithoutBraces)
        .toLower();
}

bool EncryptPassword(const crypto::SessionKey& key, const QString& plaintext,
                     QByteArray* out_ciphertext, QByteArray* out_iv) {
    const auto iv_bytes = crypto::Random::Bytes(crypto::CryptoService::kIvSize);
    const QByteArray pt = plaintext.toUtf8();
    QByteArray ct = crypto::CryptoService::EncryptGcm(
        key.data(), key.size(), iv_bytes.data(), iv_bytes.size(),
        reinterpret_cast<const std::uint8_t*>(pt.constData()),
        static_cast<std::size_t>(pt.size()));
    if (ct.isEmpty() && !pt.isEmpty()) return false;
    *out_ciphertext = std::move(ct);
    *out_iv = QByteArray(reinterpret_cast<const char*>(iv_bytes.data()),
                         static_cast<int>(iv_bytes.size()));
    return true;
}

int RandomIconColor() {
    std::uint8_t b = 0;
    crypto::Random::Fill(&b, 1);
    return b % 6;
}

}  // namespace

std::optional<ImportSummary> CsvImporter::Apply(
    const CsvValidationResult& validation,
    storage::PasswordDao& password_dao,
    storage::CategoryDao& category_dao,
    const crypto::SessionKey& session_key,
    std::int64_t now_ms) {
    ImportSummary summary;
    if (validation.valid_rows.isEmpty()) return summary;

    const auto categories = category_dao.ListActive();
    QHash<QString, std::int64_t> id_by_name_lower;
    int next_sort_order = static_cast<int>(categories.size());
    for (const auto& c : categories) {
        id_by_name_lower.insert(c.name.toLower(), c.id);
    }

    for (const auto& name : validation.new_category_names) {
        const QString key = name.toLower();
        if (id_by_name_lower.contains(key)) continue;
        model::Category cat;
        cat.uuid = QUuid::createUuid()
                       .toString(QUuid::WithoutBraces)
                       .toLower();
        cat.name = name;
        cat.color = RandomIconColor();
        cat.is_default = false;
        cat.sort_order = next_sort_order++;
        cat.created_at = now_ms;
        cat.updated_at = now_ms;
        const auto new_id = category_dao.Insert(cat);
        if (!new_id.has_value()) return std::nullopt;
        id_by_name_lower.insert(key, *new_id);
        summary.created_categories.append(name);
    }

    for (const auto& row : validation.valid_rows) {
        std::int64_t category_id = 0;
        if (row.category_name != CsvValidator::Uncategorized()) {
            category_id = id_by_name_lower.value(row.category_name.toLower(), 0);
        }

        QByteArray ct;
        QByteArray iv;
        if (!EncryptPassword(session_key, row.password, &ct, &iv)) {
            return std::nullopt;
        }

        if (row.action == ImportAction::kUpdate && row.existing_id.has_value()) {
            auto existing = password_dao.FindById(*row.existing_id);
            if (!existing.has_value()) {
                return std::nullopt;
            }
            existing->title = row.title;
            existing->username = row.username;
            existing->encrypted_password = ct;
            existing->password_iv = iv;
            existing->website = row.website;
            existing->notes = row.notes;
            existing->category_id = category_id;
            existing->strength = generator::CalculatePasswordStrength(row.password);
            existing->updated_at = now_ms;
            if (!password_dao.Update(*existing)) return std::nullopt;
            ++summary.updated;
        } else {
            model::PasswordEntry entry;
            entry.uuid = EnsureUuid(row.existing_uuid);
            entry.title = row.title;
            entry.username = row.username;
            entry.encrypted_password = ct;
            entry.password_iv = iv;
            entry.website = row.website;
            entry.notes = row.notes;
            entry.category_id = category_id;
            entry.strength = generator::CalculatePasswordStrength(row.password);
            entry.created_at = now_ms;
            entry.updated_at = now_ms;
            if (!password_dao.Insert(entry).has_value()) return std::nullopt;
            ++summary.inserted;
        }
    }

    return summary;
}

}  // namespace passvault::csv
