#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace passvault::crypto {

std::vector<std::uint8_t> Sha256(const std::uint8_t* data, std::size_t size);
std::vector<std::uint8_t> Sha256(std::string_view s);

std::string Sha256HexLower(const std::uint8_t* data, std::size_t size);
std::string Sha256HexLower(std::string_view s);

}  // namespace passvault::crypto
