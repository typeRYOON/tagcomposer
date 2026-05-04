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
    QString tag;  // normalized to space form (utils::normalizeTagInput)
    int category; // mirrors tags.csv category column (Danbooru convention)
    float score;
};

// Output of one image's inference. Rating is split out from regular tags -
// rating tags are typed via `category == 9` in the CSV, and the model emits
// them alongside the per-tag sigmoids; we report just the argmax rating.
//
// `nearMisses` carries up to ~50 next-best tags that fell *just* below the
// threshold (sorted descending). The UI shows them dimmed under the main
// list so a user lowering the threshold has a sense of what would come in.
struct TagResult {
    QList<TagPrediction> tags;       // sorted by score, descending
    QList<TagPrediction> nearMisses; // top-N below threshold, descending
    QString rating;
    float ratingScore = 0.0f;
};

// Wraps one loaded ONNX tagger session + its preprocessing config + label
// list. Thread-safe for `tag()`: ONNX Runtime supports concurrent Run()
// calls on the same session, and the preprocessing path holds no shared
// mutable state.
//
// ── Expected model directory layout ─────────────────────────────────────
//   <dir>/model.onnx     required. Input shape and layout (NCHW vs NHWC)
//                        are auto-detected from the session, so different
//                        architectures + sizes work as long as the input
//                        is a single 4-D float tensor with a 3-channel dim.
//   <dir>/config.json    required. timm-style. We read:
//                          pretrained_cfg.input_size  : [C, H, W]  (fallback
//                                                       when ONNX dims are
//                                                       dynamic)
//                          pretrained_cfg.mean / std  : informational only,
//                                                       currently unused
//                                                       (this ONNX family
//                                                       bakes normalization
//                                                       into the graph).
//   <dir>/tags.csv       required. Header line + rows of:
//                          tag_id,name,category,count
//                        Only `name` (col 1) and `category` (col 2) are
//                        consumed. Row index = output logit index. Tags
//                        with category == 9 are treated as ratings; the
//                        argmax of those is reported separately. If the
//                        CSV has no rating rows, the rating field stays
//                        empty - that's a valid model.
class AutoTaggerModel {
public:
    // Loads <dir>/{model.onnx, config.json, tags.csv}. Returns nullptr on
    // failure with `*err` populated when non-null. Shares `env` so the whole
    // app sits on a single ORT environment.
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

    // `threshold` filters non-rating tags only - rating is always populated
    // (argmax of the rating subset).
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

    // Loads + decodes + pads to square (white) + bicubic resize to (H, W)
    // + alpha-composites over white + RGB swap + (x/255 - mean) / std,
    // then arranges into NCHW or NHWC per the session's declared layout.
    std::vector<float> preprocessImage(const QString& imagePath) const;

    TagResult interpretOutput(const float* out, int64_t outSize, float threshold) const;

    QString m_name;
    QString m_dir;

    // Inference state
    std::unique_ptr<Ort::Session> m_session;
    Ort::SessionOptions m_sessionOptions;
    Layout m_layout = Layout::NCHW;
    int m_height = 448;
    int m_width = 448;

    // I/O names - kept alive on the model so the const char* we hand to
    // Run() stays valid. ORT returns these as allocator-owned C strings; we
    // copy into std::string for simpler ownership.
    std::string m_inputName;
    std::string m_outputName;

    // Preprocessing - ImageNet-style timm config: mean/std per channel,
    // applied after /255 and channel reorder to RGB.
    std::array<float, 3> m_mean{0.5f, 0.5f, 0.5f};
    std::array<float, 3> m_std{0.5f, 0.5f, 0.5f};

    // Labels
    QList<TagInfo> m_tagInfo;
    QList<int> m_ratingIndices; // category == 9
};

} // namespace core

// Register the result types so BatchTagger can emit them across threads
// (queued connections need Qt's metatype system to copy the value).
Q_DECLARE_METATYPE(core::TagPrediction)
Q_DECLARE_METATYPE(core::TagResult)
