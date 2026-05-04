#pragma once
#include <QString>
#include <cstdint>

namespace core {

// 64-bit pHash; returns 0 on decode failure (a real pHash is never 0).
// Standard recipe: grayscale -> 32x32 -> DCT -> top-left 8x8 -> median threshold.
uint64_t phashFile(const QString& imagePath);

// Bit-count of (a ^ b). Typical near-duplicate threshold: 3-6.
int hammingDistance(uint64_t a, uint64_t b);

} // namespace core
