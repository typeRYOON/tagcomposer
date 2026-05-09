#include <core/autotaggermodel.h>
#include <utils/stringutils.h>
#include <opencv2/opencv.hpp>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <algorithm>
#include <cmath>

namespace core {

// ---- Construction

std::unique_ptr<AutoTaggerModel> AutoTaggerModel::loadFromDir(Ort::Env& env, const QString& dir,
                                                              QString* err)
{
    auto setErr = [&](const QString& msg) {
        if (err) *err = msg;
    };

    const QString modelPath = dir + "/model.onnx";
    const QString configPath = dir + "/config.json";
    const QString tagsPath = dir + "/tags.csv";

    for (const QString& p : {modelPath, configPath, tagsPath}) {
        if (!QFile::exists(p)) {
            setErr(QString("missing file: %1").arg(p));
            return nullptr;
        }
    }

    std::unique_ptr<AutoTaggerModel> m(new AutoTaggerModel());
    m->m_dir = dir;
    m->m_name = QFileInfo(dir).fileName();

    if (!m->readConfig(configPath, err)) return nullptr;
    if (!m->readTags(tagsPath, err)) return nullptr;
    if (!m->initSession(env, modelPath, err)) return nullptr;

    return m;
}

bool AutoTaggerModel::readConfig(const QString& configPath, QString* err)
{
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = "could not open config.json";
        return false;
    }

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    // input_size comes back as [C, H, W]. Used only when the ONNX session
    // reports dynamic dims; the session shape otherwise wins in initSession.
    const QJsonObject pre = root["pretrained_cfg"].toObject();

    const QJsonArray sz = pre["input_size"].toArray();
    if (sz.size() == 3) {
        m_height = sz[1].toInt(m_height);
        m_width = sz[2].toInt(m_width);
    }

    const QJsonArray mean = pre["mean"].toArray();
    const QJsonArray std = pre["std"].toArray();
    if (mean.size() == 3) {
        for (int i = 0; i < 3; ++i)
            m_mean[i] = float(mean[i].toDouble(m_mean[i]));
    }
    if (std.size() == 3) {
        for (int i = 0; i < 3; ++i)
            m_std[i] = float(std[i].toDouble(m_std[i]));
    }

    return true;
}

bool AutoTaggerModel::readTags(const QString& csvPath, QString* err)
{
    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = "could not open tags.csv";
        return false;
    }

    f.readLine(); // header: tag_id,name,category,count

    int row = 0;
    while (!f.atEnd()) {
        const QString line = f.readLine().trimmed();
        if (line.isEmpty()) continue;

        const QStringList split = line.split(',');
        if (split.size() < 3) continue;

        bool ok = false;
        const int category = split.at(2).toInt(&ok, 10);
        if (!ok) continue;

        const QString tagName = utils::normalizeTagInput(split.at(1));
        m_tagInfo.append({tagName, category});
        if (category == 9) m_ratingIndices.append(row);
        ++row;
    }

    if (m_tagInfo.isEmpty()) {
        if (err) *err = "tags.csv produced zero entries";
        return false;
    }
    return true;
}

bool AutoTaggerModel::initSession(Ort::Env& env, const QString& modelPath, QString* err)
{
    try {
        m_sessionOptions.SetInterOpNumThreads(0);
        m_sessionOptions.SetIntraOpNumThreads(0);
        m_sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        // ORTCHAR_T is wchar_t on Windows, char on POSIX - encode accordingly.
#ifdef _WIN32
        const auto pathStr = modelPath.toStdWString();
#else
        const auto pathStr = modelPath.toStdString();
#endif
        m_session = std::make_unique<Ort::Session>(env, pathStr.c_str(), m_sessionOptions);

        // Copy out so we can hand stable const char* to Run().
        Ort::AllocatorWithDefaultOptions allocator;
        {
            auto in = m_session->GetInputNameAllocated(0, allocator);
            auto out = m_session->GetOutputNameAllocated(0, allocator);
            m_inputName = in.get();
            m_outputName = out.get();
        }

        // Dynamic dims show up as negative; fall back to config.json values
        // (already in m_height/m_width) for those.
        const auto info = m_session->GetInputTypeInfo(0);
        const auto shape = info.GetTensorTypeAndShapeInfo().GetShape();

        if (shape.size() == 4) {
            // 3 at dim 1 = NCHW, 3 at last dim = NHWC; default NCHW otherwise.
            const int64_t d1 = shape[1];
            const int64_t d3 = shape[3];
            if (d1 == 3) {
                m_layout = Layout::NCHW;
                if (shape[2] > 0) m_height = int(shape[2]);
                if (shape[3] > 0) m_width = int(shape[3]);
            }
            else if (d3 == 3) {
                m_layout = Layout::NHWC;
                if (shape[1] > 0) m_height = int(shape[1]);
                if (shape[2] > 0) m_width = int(shape[2]);
            }
        }
    }
    catch (const Ort::Exception& e) {
        if (err) *err = QString("ORT load failed: %1").arg(e.what());
        return false;
    }

    return true;
}

// ---- Preprocessing

std::vector<float> AutoTaggerModel::preprocessImage(const QString& imagePath) const
{
    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray buffer = file.readAll();

    cv::Mat img = cv::imdecode(cv::Mat(1, int(buffer.size()), CV_8UC1, (void*)buffer.data()),
                               cv::IMREAD_UNCHANGED);
    if (img.empty()) return {};

    // Grayscale -> 3-channel; RGBA -> drop alpha; downcast 16-bit to 8-bit.
    if (img.channels() == 1) {
        cv::cvtColor(img, img, cv::COLOR_GRAY2RGB);
    }
    else if (img.channels() == 4) {
        cv::Mat img8;
        if (img.depth() != CV_8U)
            img.convertTo(img8, CV_8U, 1.0 / 256.0);
        else
            img8 = img;
        cv::cvtColor(img8, img, cv::COLOR_BGRA2BGR);
    }
    else if (img.depth() != CV_8U) {
        cv::Mat img8;
        img.convertTo(img8, CV_8U, 1.0 / 256.0);
        img = img8;
    }

    // Pad to square with white, then bicubic resize.
    const int W = img.cols, H = img.rows;
    const int side = std::max(W, H);
    cv::Mat square(side, side, img.type(), cv::Scalar(255, 255, 255));
    img.copyTo(square(cv::Rect((side - W) / 2, (side - H) / 2, W, H)));

    cv::Mat resized;
    cv::resize(square, resized, cv::Size(m_width, m_height), 0, 0, cv::INTER_CUBIC);

    // Raw [0, 255] BGR float - DO NOT normalize. This ONNX family bakes
    // mean/std into the graph; pre-normalizing collapses activations.
    cv::Mat floatImg;
    resized.convertTo(floatImg, CV_32F);

    const int H2 = m_height, W2 = m_width;
    std::vector<float> out;
    if (m_layout == Layout::NHWC) {
        // H x W x 3 interleaved - direct memcpy.
        out.assign((float*)floatImg.datastart, (float*)floatImg.dataend);
    }
    else {
        out.resize(size_t(3) * H2 * W2);
        std::vector<cv::Mat> ch(3);
        cv::split(floatImg, ch);
        for (int c = 0; c < 3; ++c)
            std::memcpy(out.data() + size_t(c) * H2 * W2, ch[c].data, sizeof(float) * H2 * W2);
    }
    return out;
}

// ---- Inference

TagResult AutoTaggerModel::tag(const QString& imagePath, float threshold) const
{
    if (!m_session) return {};

    std::vector<float> input = preprocessImage(imagePath);
    if (input.empty()) return {};

    const std::array<int64_t, 4> shape = (m_layout == Layout::NHWC)
                                             ? std::array<int64_t, 4>{1, m_height, m_width, 3}
                                             : std::array<int64_t, 4>{1, 3, m_height, m_width};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(),
                                                             shape.data(), shape.size());

    const char* inputNames[] = {m_inputName.c_str()};
    const char* outputNames[] = {m_outputName.c_str()};

    std::vector<Ort::Value> outputs;
    try {
        outputs =
            m_session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
    }
    catch (const Ort::Exception&) {
        return {};
    }

    const float* out = outputs[0].GetTensorMutableData<float>();
    int64_t outSize = 1;
    for (int64_t d : outputs[0].GetTensorTypeAndShapeInfo().GetShape())
        if (d > 0) outSize *= d;

    return interpretOutput(out, outSize, threshold);
}

TagResult AutoTaggerModel::interpretOutput(const float* out, int64_t outSize, float threshold) const
{
    TagResult result;

    const int64_t N = std::min<int64_t>(m_tagInfo.size(), outSize);
    constexpr int kNearMissCount = 50;

    // Rating: argmax over the rating subset (only one ever applies).
    int bestRatingIdx = -1;
    float bestRatingScore = -1.0f;
    for (int idx : m_ratingIndices) {
        if (idx >= N) continue;
        if (out[idx] > bestRatingScore) {
            bestRatingScore = out[idx];
            bestRatingIdx = idx;
        }
    }
    if (bestRatingIdx >= 0) {
        result.rating = m_tagInfo[bestRatingIdx].tag;
        result.ratingScore = bestRatingScore;
    }

    QList<TagPrediction> below;
    for (int64_t i = 0; i < N; ++i) {
        const TagInfo& ti = m_tagInfo[int(i)];
        if (ti.category == 9) continue; // skip ratings here
        if (out[i] >= threshold)
            result.tags.append({ti.tag, ti.category, out[i]});
        else
            below.append({ti.tag, ti.category, out[i]});
    }

    auto byScoreDesc = [](const TagPrediction& a, const TagPrediction& b) {
        return a.score > b.score;
    };
    std::sort(result.tags.begin(), result.tags.end(), byScoreDesc);

    if (below.size() > kNearMissCount) {
        std::partial_sort(below.begin(), below.begin() + kNearMissCount, below.end(), byScoreDesc);
        below.resize(kNearMissCount);
    }
    else {
        std::sort(below.begin(), below.end(), byScoreDesc);
    }
    result.nearMisses = std::move(below);

    return result;
}

} // namespace core
