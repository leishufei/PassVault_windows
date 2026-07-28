#pragma once

#include <QHash>
#include <QString>

#include <cstdint>
#include <utility>
#include <vector>

namespace passvault::sync {

template <typename T>
struct MergeResult {
    std::vector<T> merged;
    bool has_local_changes = false;
};

template <typename T, typename GetUpdatedAt>
MergeResult<T> MergeByUuid(const QHash<QString, T>& local_map,
                           const QHash<QString, T>& cloud_map,
                           GetUpdatedAt&& get_updated_at) {
    MergeResult<T> result;
    result.merged.reserve(
        static_cast<std::size_t>(local_map.size() + cloud_map.size()));

    QHash<QString, char> visited;
    visited.reserve(local_map.size() + cloud_map.size());

    for (auto it = local_map.constBegin(); it != local_map.constEnd(); ++it) {
        const QString& uuid = it.key();
        if (visited.contains(uuid)) continue;
        visited.insert(uuid, 1);

        const auto cloud_it = cloud_map.constFind(uuid);
        if (cloud_it == cloud_map.constEnd()) {
            result.merged.push_back(it.value());
            result.has_local_changes = true;
            continue;
        }

        const std::int64_t local_ts = get_updated_at(it.value());
        const std::int64_t cloud_ts = get_updated_at(cloud_it.value());
        if (local_ts >= cloud_ts) {
            result.merged.push_back(it.value());
            if (local_ts > cloud_ts) {
                result.has_local_changes = true;
            }
        } else {
            result.merged.push_back(cloud_it.value());
        }
    }

    for (auto it = cloud_map.constBegin(); it != cloud_map.constEnd(); ++it) {
        const QString& uuid = it.key();
        if (visited.contains(uuid)) continue;
        result.merged.push_back(it.value());
    }

    return result;
}

}  // namespace passvault::sync
