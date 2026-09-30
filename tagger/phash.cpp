#include <tagger/phash.h>
#include <opencv2/opencv.hpp>
#include <QFile>
#include <bit>
#include <vector>

namespace tc {
namespace {

// Byte-for-byte the reference phash.cc recipe, so hashes stay comparable with
// the ones the old tool wrote.
uint64_t phashOf(const cv::Mat& input)
{
    if (input.empty()) return 0;

    cv::Mat gray;
    if (input.channels() >= 3)
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    else
        gray = input;

    if (gray.depth() != CV_8U) {
        cv::Mat converted;
        gray.convertTo(converted, CV_8U);
        gray = converted;
    }

    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(32, 32));
    resized.convertTo(resized, CV_32F);

    cv::Mat transformed;
    cv::dct(resized, transformed);
    const cv::Mat low = transformed(cv::Rect(0, 0, 8, 8)).clone();

    // [0,0] is the DC term and dwarfs the rest, so it is left out of the
    // median -- including it would push the threshold past almost every
    // coefficient and flatten the hash.
    std::vector<float> values;
    values.reserve(63);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            if (i != 0 || j != 0) values.push_back(low.at<float>(i, j));

    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    const float median = values[values.size() / 2];

    uint64_t hash = 0;
    int bit = 0;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j, ++bit)
            if (low.at<float>(i, j) > median) hash |= 1ULL << bit;

    return hash;
}

} // namespace

uint64_t phashFile(const QString& imagePath)
{
    // Read and decode by hand rather than cv::imread: imread takes a local
    // 8-bit path, which loses any non-ASCII directory on Windows.
    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) return 0;

    const QByteArray buffer = file.readAll();
    if (buffer.isEmpty()) return 0;

    cv::Mat encoded(1, int(buffer.size()), CV_8UC1, const_cast<char*>(buffer.data()));
    return phashOf(cv::imdecode(encoded, cv::IMREAD_UNCHANGED));
}

int hammingDistance(uint64_t a, uint64_t b)
{
    return int(std::popcount(a ^ b));
}

} // namespace tc
