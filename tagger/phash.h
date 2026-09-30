#pragma once
#include <QString>
#include <cstdint>

namespace tc {

// 64-bit DCT perceptual hash: 32x32 gray, top-left 8x8, median threshold.
// Returns 0 if the image can't be decoded.
uint64_t phashFile(const QString& imagePath);

// Bits that differ. Near-duplicates usually land within 3 to 6.
int hammingDistance(uint64_t a, uint64_t b);

} // namespace tc
