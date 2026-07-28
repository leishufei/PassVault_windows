#pragma once

#include <QString>
#include <cstdint>

namespace passvault::model {

struct Category {
    std::int64_t id = 0;
    QString uuid;
    QString name;
    std::int32_t color = 0;
    bool is_default = false;
    std::int32_t sort_order = 0;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    bool is_deleted = false;
};

}  // namespace passvault::model
