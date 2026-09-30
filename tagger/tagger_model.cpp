#include <tagger/tagger_model.h>
#include <core/entry.h>
#include <opencv2/opencv.hpp>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// Below-threshold tags to show dimmed.
constexpr int kNearMissCount = 50;

constexpr int kRatingCategory = 9;

} // namespace

std::unique_ptr<TaggerModel> TaggerModel::load(Ort::Env& env, const QString& dir, QString* error)
{
    const QString modelPath = dir + u"/model.onnx"_s;
    const QString configPath = dir + u"/config.json"_s;
    const QString tagsPath = dir + u"/tags.csv"_s;

    for (const QString& path : {modelPath, configPath, tagsPath}) {
        if (QFile::exists(path)) continue;
        if (error) *error = u"missing file: %1"_s.arg(path);
        return nullptr;
    }

    std::unique_ptr<TaggerModel> model(new TaggerModel());
    model->m_dir = dir;
    model->m_name = QFileInfo(dir).fileName();

    if (!model->readConfig(configPath, error)) return nullptr;
    if (!model->readTags(tagsPath, error)) return nullptr;
    if (!model->initSession(env, modelPath, error)) return nullptr;
    return model;
}

QString TaggerModel::name() const
{
    return m_name;
}

QString TaggerModel::directory() const
{
    return m_dir;
}

int TaggerModel::classCount() const
{
    return int(m_tagInfo.size());
}

bool TaggerModel::readConfig(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = u"could not open config.json"_s;
        return false;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject pretrained = root[u"pretrained_cfg"_s].toObject();

    // [C, H, W]; initSession prefers the session's own size.
    const QJsonArray size = pretrained[u"input_size"_s].toArray();
    if (size.size() == 3) {
        m_height = size[1].toInt(m_height);
        m_width = size[2].toInt(m_width);
    }

    const QJsonArray mean = pretrained[u"mean"_s].toArray();
    const QJsonArray deviation = pretrained[u"std"_s].toArray();
    if (mean.size() == 3)
        for (int i = 0; i < 3; ++i) m_mean[i] = float(mean[i].toDouble(m_mean[i]));
    if (deviation.size() == 3)
        for (int i = 0; i < 3; ++i) m_std[i] = float(deviation[i].toDouble(m_std[i]));

    return true;
}

bool TaggerModel::readTags(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = u"could not open tags.csv"_s;
        return false;
    }

    file.readLine(); // header: tag_id,name,category,count

    // The row index is the output index.
    int row = 0;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) continue;

        const QStringList fields = line.split(u',');
        if (fields.size() < 3) continue;

        bool ok = false;
        const int category = fields.at(2).toInt(&ok, 10);
        if (!ok) continue;

        m_tagInfo.append({normalizeTag(fields.at(1)), category});
        if (category == kRatingCategory) m_ratingIndices.append(row);
        ++row;
    }

    if (m_tagInfo.isEmpty()) {
        if (error) *error = u"tags.csv produced zero entries"_s;
        return false;
    }
    return true;
}

bool TaggerModel::initSession(Ort::Env& env, const QString& path, QString* error)
{
    try {
        m_sessionOptions.SetInterOpNumThreads(0);
        m_sessionOptions.SetIntraOpNumThreads(0);
        m_sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        // ORTCHAR_T is wchar_t on Windows and char everywhere else.
#ifdef _WIN32
        const std::wstring nativePath = path.toStdWString();
#else
        const std::string nativePath = path.toStdString();
#endif
        m_session = std::make_unique<Ort::Session>(env, nativePath.c_str(), m_sessionOptions);

        Ort::AllocatorWithDefaultOptions allocator;
        m_inputName = m_session->GetInputNameAllocated(0, allocator).get();
        m_outputName = m_session->GetOutputNameAllocated(0, allocator).get();

        const auto shape = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() == 4) {
            // The dim that is 3 is channels. Dynamic dims (negative) keep the config values.
            if (shape[1] == 3) {
                m_layout = Layout::NCHW;
                if (shape[2] > 0) m_height = int(shape[2]);
                if (shape[3] > 0) m_width = int(shape[3]);
            }
            else if (shape[3] == 3) {
                m_layout = Layout::NHWC;
                if (shape[1] > 0) m_height = int(shape[1]);
                if (shape[2] > 0) m_width = int(shape[2]);
            }
        }
    }
    catch (const Ort::Exception& e) {
        if (error) *error = u"ONNX load failed: %1"_s.arg(QString::fromUtf8(e.what()));
        return false;
    }
    return true;
}

std::vector<float> TaggerModel::preprocess(const QString& imagePath) const
{
    // Not cv::imread: it fails on non-ASCII paths on Windows.
    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    const QByteArray buffer = file.readAll();
    cv::Mat image =
        cv::imdecode(cv::Mat(1, int(buffer.size()), CV_8UC1, const_cast<char*>(buffer.data())),
                     cv::IMREAD_UNCHANGED);
    if (image.empty()) return {};

    // Normalize to 8-bit, 3-channel.
    if (image.channels() == 1) {
        cv::cvtColor(image, image, cv::COLOR_GRAY2RGB);
    }
    else if (image.channels() == 4) {
        cv::Mat eightBit;
        if (image.depth() != CV_8U)
            image.convertTo(eightBit, CV_8U, 1.0 / 256.0);
        else
            eightBit = image;
        cv::cvtColor(eightBit, image, cv::COLOR_BGRA2BGR);
    }
    else if (image.depth() != CV_8U) {
        cv::Mat eightBit;
        image.convertTo(eightBit, CV_8U, 1.0 / 256.0);
        image = eightBit;
    }

    // Pad to a white square to keep the aspect ratio.
    const int width = image.cols;
    const int height = image.rows;
    const int side = std::max(width, height);

    cv::Mat square(side, side, image.type(), cv::Scalar(255, 255, 255));
    image.copyTo(square(cv::Rect((side - width) / 2, (side - height) / 2, width, height)));

    cv::Mat resized;
    cv::resize(square, resized, cv::Size(m_width, m_height), 0, 0, cv::INTER_CUBIC);

    // Raw [0, 255] BGR; the graph normalizes.
    cv::Mat asFloat;
    resized.convertTo(asFloat, CV_32F);

    if (m_layout == Layout::NHWC)
        return {reinterpret_cast<const float*>(asFloat.datastart),
                reinterpret_cast<const float*>(asFloat.dataend)};

    std::vector<float> planar(size_t(3) * m_height * m_width);
    std::vector<cv::Mat> channels(3);
    cv::split(asFloat, channels);
    for (int c = 0; c < 3; ++c)
        std::memcpy(planar.data() + size_t(c) * m_height * m_width, channels[c].data,
                    sizeof(float) * m_height * m_width);
    return planar;
}

TaggerResult TaggerModel::tag(const QString& imagePath, float threshold) const
{
    if (!m_session) return {};

    std::vector<float> input = preprocess(imagePath);
    if (input.empty()) return {};

    const std::array<int64_t, 4> shape =
        m_layout == Layout::NHWC ? std::array<int64_t, 4>{1, m_height, m_width, 3}
                                 : std::array<int64_t, 4>{1, 3, m_height, m_width};

    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                        shape.data(), shape.size());

    const char* inputNames[] = {m_inputName.c_str()};
    const char* outputNames[] = {m_outputName.c_str()};

    std::vector<Ort::Value> outputs;
    try {
        outputs = m_session->Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1, outputNames, 1);
    }
    catch (const Ort::Exception&) {
        return {};
    }

    int64_t size = 1;
    for (const int64_t dim : outputs[0].GetTensorTypeAndShapeInfo().GetShape())
        if (dim > 0) size *= dim;

    return interpret(outputs[0].GetTensorMutableData<float>(), size, threshold);
}

TaggerResult TaggerModel::interpret(const float* output, int64_t size, float threshold) const
{
    TaggerResult result;

    // Guard against tags.csv and the graph disagreeing.
    const int64_t count = std::min<int64_t>(m_tagInfo.size(), size);

    // The rating is the argmax over rating rows, regardless of threshold.
    int bestRating = -1;
    float bestScore = -1.0f;
    for (const int index : m_ratingIndices) {
        if (index >= count || output[index] <= bestScore) continue;
        bestScore = output[index];
        bestRating = index;
    }
    if (bestRating >= 0) {
        result.rating = m_tagInfo[bestRating].tag;
        result.ratingScore = bestScore;
    }

    QList<TaggerPrediction> below;
    for (int64_t i = 0; i < count; ++i) {
        const TagInfo& info = m_tagInfo[qsizetype(i)];
        if (info.category == kRatingCategory) continue; // reported separately

        if (output[i] >= threshold)
            result.tags.append({info.tag, info.category, output[i]});
        else
            below.append({info.tag, info.category, output[i]});
    }

    auto byScore = [](const TaggerPrediction& a, const TaggerPrediction& b) { return a.score > b.score; };
    std::sort(result.tags.begin(), result.tags.end(), byScore);

    if (below.size() > kNearMissCount) {
        std::partial_sort(below.begin(), below.begin() + kNearMissCount, below.end(), byScore);
        below.resize(kNearMissCount);
    }
    else {
        std::sort(below.begin(), below.end(), byScore);
    }
    result.nearMisses = std::move(below);

    return result;
}

} // namespace tc
