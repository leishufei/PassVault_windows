#include "generator/password_generator.h"

#include <QByteArray>

#include <cstdint>

#include "crypto/random.h"

namespace passvault::generator {

namespace {

QByteArray BuildCharPool(const PasswordConfig& config) {
    QByteArray pool;
    if (config.include_uppercase) pool.append(PasswordGenerator::kUppercase);
    if (config.include_lowercase) pool.append(PasswordGenerator::kLowercase);
    if (config.include_numbers) pool.append(PasswordGenerator::kNumbers);
    if (config.include_symbols) pool.append(PasswordGenerator::kSymbols);
    return pool;
}

std::size_t UniformIndex(std::size_t upper) {
    const std::size_t threshold = 256U - (256U % upper);
    while (true) {
        std::uint8_t b = 0;
        crypto::Random::Fill(&b, 1);
        if (b < threshold) return b % upper;
    }
}

}  // namespace

std::optional<QString> PasswordGenerator::Generate(const PasswordConfig& config) {
    if (config.length <= 0) return std::nullopt;
    const QByteArray pool = BuildCharPool(config);
    if (pool.isEmpty()) return std::nullopt;

    QString out;
    out.reserve(config.length);
    for (int i = 0; i < config.length; ++i) {
        const std::size_t idx = UniformIndex(static_cast<std::size_t>(pool.size()));
        out.append(QLatin1Char(pool.at(static_cast<int>(idx))));
    }
    return out;
}

}  // namespace passvault::generator
