#pragma once

#include <QChar>
#include <QString>

namespace passvault::generator {

inline int CalculatePasswordStrength(const QString& password) {
    bool has_upper = false;
    bool has_lower = false;
    bool has_digit = false;
    bool has_special = false;
    for (const QChar ch : password) {
        if (ch.isUpper()) has_upper = true;
        if (ch.isLower()) has_lower = true;
        if (ch.isDigit()) has_digit = true;
        if (!ch.isLetterOrNumber()) has_special = true;
    }
    const int type_count = (has_upper ? 1 : 0) + (has_lower ? 1 : 0) +
                           (has_digit ? 1 : 0) + (has_special ? 1 : 0);
    const int length = password.size();
    if (length >= 12 && type_count >= 3) return 3;
    if (length >= 8 && type_count >= 2) return 2;
    return 1;
}

}  // namespace passvault::generator
