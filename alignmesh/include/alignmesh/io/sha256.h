#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace alignmesh::io {

/// Compute SHA-256 hash and return as a 64-character lowercase hex string.
std::string sha256_hex(const uint8_t* data, std::size_t size);

inline std::string sha256_hex(const std::vector<uint8_t>& data) {
    return sha256_hex(data.data(), data.size());
}

} // namespace alignmesh::io
