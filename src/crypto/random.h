#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace passvault::crypto {

class Random {
 public:
    static void Fill(void* buf, std::size_t size);
    static std::vector<std::uint8_t> Bytes(std::size_t size);
};

}  // namespace passvault::crypto
