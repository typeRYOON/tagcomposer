#pragma once
#include <app/chromed_dialog.h>
#include <core/workflow.h>
#include <QImage>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

namespace tc {

class ClipCanvas;
class WorkflowInputCache;

// The editor for an image-typed workflow variable. Rect, brush, bucket and
// mask-fill all paint into one grayscale mask; the erase button is a modifier
// on whichever tool is active rather than a tool of its own.
//
// On accept the mask is written to the input cache and the new maskId comes
// back through result(). Cleaning up the previous maskId is the caller's job,
// because only it knows whether the old one is still referenced.
class ClipEditorDialog : public ChromedDialog {
    Q_OBJECT

public:
    ClipEditorDialog(const QImage& source, const ImageEdits& initial, WorkflowInputCache* cache,
                     QWidget* parent = nullptr);

    // Meaningful only after exec() returned Accepted.
    ImageEdits result() const;

protected:
    void accept() override;

private:
    void updateRectLabel();
    void updateToolControls();

    // Trim mode forces Rect and locks out the painting tools, because alpha
    // is 255 everywhere and they would silently do nothing. The mask itself
    // is left alone; clearing it belongs to the toggle handler.
    void applyTrimModeUI(bool on);

    WorkflowInputCache* m_cache = nullptr;
    ImageEdits m_result;

    ClipCanvas* m_canvas = nullptr;
    QButtonGroup* m_toolGroup = nullptr;
    QPushButton* m_eraseBtn = nullptr; // a modifier, so not in the group
    QSlider* m_brushSize = nullptr;
    QSlider* m_tolerance = nullptr;
    QLabel* m_brushLabel = nullptr;
    QLabel* m_toleranceLabel = nullptr;
    QPushButton* m_invertBtn = nullptr;
    QCheckBox* m_trimToCrop = nullptr;
    QLabel* m_rectLabel = nullptr;
};

} // namespace tc
