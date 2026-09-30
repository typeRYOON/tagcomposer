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

// Mask editor for an image workflow variable: rect, brush, bucket and mask fill
// paint one grayscale mask; erase modifies the active tool. On accept the mask
// is saved to the cache; removing the old mask is the caller's job.
class ClipEditorDialog : public ChromedDialog {
    Q_OBJECT

public:
    ClipEditorDialog(const QImage& source, const ImageEdits& initial, WorkflowInputCache* cache,
                     QWidget* parent = nullptr);

    // Valid after exec() returns Accepted.
    ImageEdits result() const;

protected:
    void accept() override;

private:
    void updateRectLabel();
    void updateToolControls();

    // Trim mode forces Rect and disables the painting tools, which would do nothing.
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
