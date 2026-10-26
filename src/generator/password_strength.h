#pragma once

#include <QChar>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

namespace passvault::generator {

inline int CalculatePasswordStrength(const QString& password) {
    if (password.size() < 8) return 1;
    bool has_upper = false;
    bool has_lower = false;
    bool has_digit = false;
    bool has_special = false;
    QSet<QChar> distinct;
    for (const QChar ch : password) {
        distinct.insert(ch);
        if (ch.isUpper()) has_upper = true;
        if (ch.isLower()) has_lower = true;
        if (ch.isDigit()) has_digit = true;
        if (!ch.isLetterOrNumber()) has_special = true;
    }
    const QString folded = password.toCaseFolded();
    std::vector<qsizetype> prefix(folded.size(), 0);
    for (qsizetype i = 1; i < folded.size(); ++i) {
        qsizetype matched = prefix[i - 1];
        while (matched > 0 && folded[i] != folded[matched]) {
            matched = prefix[matched - 1];
        }
        if (folded[i] == folded[matched]) ++matched;
        prefix[i] = matched;
    }
    const qsizetype period = folded.size() - prefix.back();
    const qsizetype pattern_length = period <= folded.size() / 2
        ? period : folded.size();
    qsizetype effective_length = 1;
    for (qsizetype i = 1; i < pattern_length; ++i) {
        const int step = folded[i].unicode() - folded[i - 1].unicode();
        if (step == 0) continue;
        if (i >= 2 && (step == 1 || step == -1) &&
            step == folded[i - 1].unicode() - folded[i - 2].unicode()) {
            continue;
        }
        ++effective_length;
    }
    const qsizetype character_pool = std::max(
        distinct.size(), qsizetype((has_upper ? 26 : 0) +
            (has_lower ? 26 : 0) + (has_digit ? 10 : 0) +
            (has_special ? 32 : 0)));
    const qsizetype pool = std::min(character_pool, distinct.size() * 2);
    // This is a heuristic estimate, not a measurement of actual entropy.
    const double estimated_bits = effective_length * std::log2(pool);
    if (estimated_bits >= 70) return 3;
    return estimated_bits >= 40 ? 2 : 1;
}

}  // namespace passvault::generator
