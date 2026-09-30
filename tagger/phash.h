#pragma once
#include <QString>
#include <cstdint>

namespace tc {

// 64-bit perceptual hash: grayscale, 32x32, DCT, keep the top-left 8x8, then
// threshold against its median. Two visually similar images differ in only a
// few bits, which is what makes near-duplicate detection cheap.
//
// Returns 0 when the image cannot be decoded. A real hash is never 0.
uint64_t phashFile(const QString& imagePath);

// Bits that differ. Near-duplicates usually land within 3 to 6.
int hammingDistance(uint64_t a, uint64_t b);

} // namespace tc
