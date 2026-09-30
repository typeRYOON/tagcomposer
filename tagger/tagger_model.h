#pragma once
#include <onnxruntime_cxx_api.h>
#include <QList>
#include <QMetaType>
#include <QString>
#include <array>
#include <memory>

namespace tc {

struct TaggerPrediction {
    QString tag;
    int category; // Danbooru category number
    float score;
};

struct TaggerResult {
    QList<TaggerPrediction> tags;       // descending by score
    QList<TaggerPrediction> nearMisses; // best scores below threshold
    QString rating;
    float ratingScore = 0.0f;
};

// One ONNX tagger session; tag() is thread safe. The model directory holds
// model.onnx (4-D float input, NCHW or NHWC, size read from the session),
// config.json (timm-style; input_size covers dynamic dims) and tags.csv
// (row index = output index, category 9 = rating).
class TaggerModel {
public:
    static std::unique_ptr<TaggerModel> load(Ort::Env& env, const QString& dir,
                                             QString* error = nullptr);

    QString name() const;
    QString directory() const;
    int classCount() const;

    // threshold filters tags; the rating is always reported.
    TaggerResult tag(const QString& imagePath, float threshold) const;

private:
    TaggerModel() = default;

    struct TagInfo {
        QString tag;
        int category;
    };
    enum class Layout { NCHW, NHWC };

    bool readConfig(const QString& path, QString* error);
    bool readTags(const QString& path, QString* error);
    bool initSession(Ort::Env& env, const QString& path, QString* error);

    std::vector<float> preprocess(const QString& imagePath) const;
    TaggerResult interpret(const float* output, int64_t size, float threshold) const;

    QString m_name;
    QString m_dir;

    std::unique_ptr<Ort::Session> m_session;
    Ort::SessionOptions m_sessionOptions;
    Layout m_layout = Layout::NCHW;
    int m_height = 448;
    int m_width = 448;

    // Backing storage for the names passed to Run().
    std::string m_inputName;
    std::string m_outputName;

    // From config.json, unused: these models normalize inside the graph.
    std::array<float, 3> m_mean{0.5f, 0.5f, 0.5f};
    std::array<float, 3> m_std{0.5f, 0.5f, 0.5f};

    QList<TagInfo> m_tagInfo;
    QList<int> m_ratingIndices; // rows whose category is 9
};

} // namespace tc

// Passed across threads by BatchTagger.
Q_DECLARE_METATYPE(tc::TaggerPrediction)
Q_DECLARE_METATYPE(tc::TaggerResult)
