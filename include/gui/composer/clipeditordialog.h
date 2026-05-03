#pragma once
#include <core/workflowmanager.h>  // for ImageEdits
#include <gui/chromeddialog.h>
#include <QImage>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

namespace core { class WorkflowInputCache; }

namespace gui {

class ClipCanvas;

// Modal editor for an Image-typed workflow var. Phase 2: rect / brush /
// bucket-fill / eraser tools, all painting into a single grayscale mask.
// On accept, the mask is saved to the input cache and a fresh maskId is
// returned via result(); the caller is responsible for cleaning up any
// previous maskId the var was pointing at.
class ClipEditorDialog : public ChromedDialog {
    Q_OBJECT
public:
    ClipEditorDialog(const QImage&            source,
                     const core::ImageEdits&  initial,
                     core::WorkflowInputCache* cache,
                     QWidget*                 parent = nullptr);

    // Valid only after exec() returns Accepted.
    core::ImageEdits result() const { return m_result; }

protected:
    void accept() override;

private:
    void updateRectLabel();
    void updateToolControls();
    // Adjusts the tool palette to match trim/mask mode: trim mode forces
    // Rect, disables Brush/Bucket/Erase (they have no effect when alpha is
    // forced to 255 everywhere). Doesn't touch the mask itself — clearing
    // the mask on user-initiated toggle is handled in the toggled callback.
    void applyTrimModeUI(bool on);

    core::WorkflowInputCache* m_cache = nullptr;
    core::ImageEdits          m_result;

    ClipCanvas*   m_canvas      = nullptr;
    QButtonGroup* m_toolGroup   = nullptr;
    QPushButton*  m_eraseBtn    = nullptr;   // modifier toggle, not in toolGroup
    QSlider*      m_brushSize   = nullptr;
    QSlider*      m_tolerance   = nullptr;
    QLabel*       m_brushLabel  = nullptr;
    QLabel*       m_tolLabel    = nullptr;
    QPushButton*  m_invertBtn   = nullptr;
    QCheckBox*    m_trimToCrop  = nullptr;
    QLabel*       m_rectLabel   = nullptr;
};

} // namespace gui
