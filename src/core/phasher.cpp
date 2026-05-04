#include <core/phasher.h>
#include <opencv2/opencv.hpp>
#include <QFile>
#include <bit>

namespace core {

namespace {

// Computes a 64-bit pHash from an in-memory cv::Mat. Direct port of the
// reference phash.cc - kept identical to the implementation we already
// validated, so a hash from this app is byte-equivalent to one from the
// standalone tool.
uint64_t phashOf(const cv::Mat& input)
{
    if (input.empty()) return 0;

    cv::Mat gray;
    if (input.channels() >= 3)
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    else
        gray = input;

    if (gray.depth() != CV_8U) {
        cv::Mat g8;
        gray.convertTo(g8, CV_8U);
        gray = g8;
    }

    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(32, 32));
    resized.convertTo(resized, CV_32F);

    cv::Mat dct_img;
    cv::dct(resized, dct_img);

    const cv::Mat dct_low = dct_img(cv::Rect(0, 0, 8, 8)).clone();

    // Median over the 8x8 block, skipping the DC coefficient at [0,0]
    // (its huge magnitude would skew the median otherwise).
    std::vector<float> vals;
    vals.reserve(63);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            if (!(i == 0 && j == 0)) vals.push_back(dct_low.at<float>(i, j));

    std::nth_element(vals.begin(), vals.begin() + vals.size() / 2, vals.end());
    const float median = vals[vals.size() / 2];

    uint64_t hash = 0;
    int bit = 0;
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            if (dct_low.at<float>(i, j) > median) hash |= (1ULL << bit);
            ++bit;
        }
    }
    return hash;
}

} // namespace

uint64_t phashFile(const QString& imagePath)
{
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QByteArray buffer = f.readAll();
    if (buffer.isEmpty()) return 0;

    // imdecode handles every format OpenCV was built with - png/jpg/webp/bmp/gif.
    cv::Mat data(1, int(buffer.size()), CV_8UC1, (void*)buffer.data());
    cv::Mat img = cv::imdecode(data, cv::IMREAD_UNCHANGED);
    if (img.empty()) return 0;

    return phashOf(img);
}

int hammingDistance(uint64_t a, uint64_t b)
{
    return int(std::popcount(a ^ b));
}

} // namespace core
