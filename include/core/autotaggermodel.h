#pragma once
#include <onnxruntime_cxx_api.h>
#include <QString>
#include <QList>
#include <QMetaType>
#include <array>
#include <memory>

class QImage;

namespace core {

struct TagPrediction {
    QString tag;  // normalized via utils::normalizeTagInput
    int category; // tags.csv category column (Danbooru convention)
    float score;
};

// Rating is split out: tags with category == 9 are typed as ratings; we
// report only the argmax. nearMisses are the top-N tags that fell *just*
// below threshold (UI shows them dimmed).
struct TagResult {
    QList<TagPrediction> tags;       // descending by score
    QList<TagPrediction> nearMisses; // descending by score
    QString rating;
    float ratingScore = 0.0f;
};

// One loaded ONNX session + preprocessing config + labels. tag() is thread
// safe; ORT handles concurrent Run() on the same session and there's no
// shared mutable state in preprocessing.
//
// Directory layout:
//   model.onnx   - input is a single 4-D float tensor with a 3-channel dim;
//                  layout (NCHW/NHWC) and H/W auto-detected from the session
//   config.json  - timm-style. pretrained_cfg.input_size as [C,H,W] is the
//                  fallback when ONNX dims are dynamic. mean/std are read
//                  but informational; this ONNX family bakes normalization
//                  into the graph itself.
//   tags.csv     - header + rows tag_id,name,category,count. Row index is
//                  the output logit index. category == 9 means rating.
class AutoTaggerModel {
public:
    static std::unique_ptr<AutoTaggerModel> loadFromDir(Ort::Env& env, const QString& dir,
                                                        QString* err = nullptr);

    QString name() const
    {
        return m_name;
    }
    QString directory() const
    {
        return m_dir;
    }
    int numClasses() const
    {
        return int(m_tagInfo.size());
    }

    // threshold filters tags only; rating is always populated.
    TagResult tag(const QString& imagePath, float threshold) const;

private:
    AutoTaggerModel() = default;

    struct TagInfo {
        QString tag;
        int category;
    };
    enum class Layout { NCHW, NHWC };

    bool readConfig(const QString& configPath, QString* err);
    bool readTags(const QString& csvPath, QString* err);
    bool initSession(Ort::Env& env, const QString& modelPath, QString* err);

    std::vector<float> preprocessImage(const QString& imagePath) const;
    TagResult interpretOutput(const float* out, int64_t outSize, float threshold) const;

    QString m_name;
    QString m_dir;

    std::unique_ptr<Ort::Session> m_session;
    Ort::SessionOptions m_sessionOptions;
    Layout m_layout = Layout::NCHW;
    int m_height = 448;
    int m_width = 448;

    // Owns the strings whose const char* we hand to Run().
    std::string m_inputName;
    std::string m_outputName;

    std::array<float, 3> m_mean{0.5f, 0.5f, 0.5f};
    std::array<float, 3> m_std{0.5f, 0.5f, 0.5f};

    QList<TagInfo> m_tagInfo;
    QList<int> m_ratingIndices; // tags with category == 9
};

} // namespace core

// BatchTagger emits these across threads via queued connections.
Q_DECLARE_METATYPE(core::TagPrediction)
Q_DECLARE_METATYPE(core::TagResult)
