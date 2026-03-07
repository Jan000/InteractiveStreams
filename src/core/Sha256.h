#pragma once
// Minimal SHA-256 implementation (FIPS 180-4) – public domain algorithm.
// Header-only, no dependencies beyond the C++ standard library.

#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>

namespace is::core {

namespace detail {

static constexpr uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z)   { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z)  { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sig0(uint32_t x) { return rotr(x, 2)  ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t sig1(uint32_t x) { return rotr(x, 6)  ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t gam0(uint32_t x) { return rotr(x, 7)  ^ rotr(x, 18) ^ (x >> 3);    }
inline uint32_t gam1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);   }

} // namespace detail

/// Compute SHA-256 of arbitrary data and return as 64-char hex string.
inline std::string sha256(const void* data, size_t len) {
    using namespace detail;

    // Initial hash values (first 32 bits of fractional parts of square roots of first 8 primes)
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    auto bytes = static_cast<const uint8_t*>(data);
    uint64_t bitLen = static_cast<uint64_t>(len) * 8;

    // Pre-processing: pad to 64-byte blocks
    // message + 0x80 + zeros + 8-byte big-endian length
    size_t padded = ((len + 9 + 63) / 64) * 64;
    std::vector<uint8_t> buf(padded, 0);
    std::memcpy(buf.data(), bytes, len);
    buf[len] = 0x80;
    for (int i = 0; i < 8; ++i) {
        buf[padded - 1 - i] = static_cast<uint8_t>(bitLen >> (i * 8));
    }

    // Process each 64-byte block
    for (size_t offset = 0; offset < padded; offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(buf[offset + i*4]) << 24)
                 | (uint32_t(buf[offset + i*4+1]) << 16)
                 | (uint32_t(buf[offset + i*4+2]) << 8)
                 |  uint32_t(buf[offset + i*4+3]);
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = gam1(w[i-2]) + w[i-7] + gam0(w[i-15]) + w[i-16];
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + sig1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = sig0(a) + maj(a, b, c);
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    // Format as hex
    std::ostringstream oss;
    for (int i = 0; i < 8; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(8) << h[i];
    }
    return oss.str();
}

/// Convenience overload for std::string input.
inline std::string sha256(const std::string& s) {
    return sha256(s.data(), s.size());
}

/// Hash a password with a salt: SHA-256(salt + password).
inline std::string hashPassword(const std::string& password, const std::string& salt) {
    std::string combined = salt + password;
    return sha256(combined);
}

/// Generate a random hex salt of the given byte count.
inline std::string generateSalt(int bytes = 16) {
    std::random_device rd;
    std::ostringstream oss;
    for (int i = 0; i < bytes; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << (rd() & 0xFF);
    }
    return oss.str();
}

/// Generate a random session token (32 hex chars).
inline std::string generateToken() {
    return generateSalt(16);
}

} // namespace is::core
