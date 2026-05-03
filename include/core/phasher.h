#pragma once
#include <QString>
#include <cstdint>

namespace core {

// 64-bit perceptual hash of an image file. Returns 0 on failure (decode
// error, missing/corrupt file, unsupported format). 0 is unreachable as a
// valid pHash for non-trivial inputs (DCT median split is essentially never
// "all zeroes"), so callers can treat 0 as a sentinel.
//
// Implementation matches the standard pHash recipe used by `imagehash` /
// imagehasher.py: grayscale → 32×32 resize → DCT → top-left 8×8 → median
// threshold → 64-bit hash. Hamming distance against another phashFile
// result is comparable to imagehash's `hash - hash` operation.
uint64_t phashFile(const QString& imagePath);

// Number of differing bits between two 64-bit hashes. 0 means identical
// hashes (very likely the same image); typical "near-duplicate" thresholds
// for 64-bit pHash sit at 3–6.
int hammingDistance(uint64_t a, uint64_t b);

} // namespace core
