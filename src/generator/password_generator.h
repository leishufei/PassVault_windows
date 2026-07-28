#pragma once

#include <QString>

#include <optional>

namespace passvault::generator {

struct PasswordConfig {
    int length = 16;
    bool include_uppercase = true;
    bool include_lowercase = true;
    bool include_numbers = true;
    bool include_symbols = true;
};

class PasswordGenerator {
 public:
    static constexpr char kUppercase[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr char kLowercase[] = "abcdefghijklmnopqrstuvwxyz";
    static constexpr char kNumbers[] = "0123456789";
    static constexpr char kSymbols[] = "!@#$%^&*()_+-=[]{}|;:,.<>?";

    static std::optional<QString> Generate(const PasswordConfig& config);
};

}  // namespace passvault::generator
