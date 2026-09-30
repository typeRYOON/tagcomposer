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
    int category; // the tags.csv column, following Danbooru's numbering
    float score;
};

struct TaggerResult {
    QList<TaggerPrediction> tags;       // descending by score
    QList<TaggerPrediction> nearMisses; // the best of what fell below threshold
    QString rating;
    float ratingScore = 0.0f;
};

// One loaded ONNX session with its preprocessing settings and labels.
//
// tag() is thread safe: ONNX Runtime allows concurrent Run() on one session,
// and preprocessing keeps no shared state.
//
// The directory holds three files:
//   model.onnx   one 4-D float input with a channel dim of 3. The layout
//                (NCHW or NHWC) and the height and width are read off the
//                session rather than assumed.
//   config.json  timm-style. pretrained_cfg.input_size is only consulted
//                when the session reports a dynamic dimension.
//   tags.csv     header plus tag_id,name,category,count. The row index is
//                the output index, and category 9 means rating.
class TaggerModel {
public:
    static std::unique_ptr<TaggerModel> load(Ort::Env& env, const QString& dir,
                                             QString* error = nullptr);

    QString name() const;
    QString directory() const;
    int classCount() const;

    // The threshold filters tags only. A rating is always reported.
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

    // Owns the strings behind the const char* handed to Run().
    std::string m_inputName;
    std::string m_outputName;

    // Read from config.json and kept for reference. This model family bakes
    // normalisation into the graph, so preprocessing does not apply them.
    std::array<float, 3> m_mean{0.5f, 0.5f, 0.5f};
    std::array<float, 3> m_std{0.5f, 0.5f, 0.5f};

    QList<TagInfo> m_tagInfo;
    QList<int> m_ratingIndices; // rows whose category is 9
};

} // namespace tc

// BatchTagger hands these between threads through queued connections.
Q_DECLARE_METATYPE(tc::TaggerPrediction)
Q_DECLARE_METATYPE(tc::TaggerResult)
