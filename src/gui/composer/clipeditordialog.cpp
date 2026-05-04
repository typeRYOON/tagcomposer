#include <gui/composer/clipeditordialog.h>
#include <core/workflowinputcache.h>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace gui {

namespace {

// Eraser is no longer a separate tool - it's a modifier on the active tool.
// Rect+erase wipes a rectangle out of the mask, Brush+erase paints clear,
// Bucket+erase removes the flood-filled region.
enum class Tool { Rect, Brush, Bucket };

// Compute the tight bounding box of non-zero pixels in a Grayscale8 mask.
// Returns a null QRect if the mask is empty/all-zero.
QRect maskBoundingBox(const QImage& mask)
{
    if (mask.isNull()) return {};
    int minX = mask.width(), minY = mask.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (row[x]) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) return {};
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

} // namespace

// ── Canvas ──────────────────────────────────────────────────────────────────

class ClipCanvas : public QWidget {
public:
    explicit ClipCanvas(const QImage& source, QWidget* parent = nullptr)
        : QWidget(parent), m_source(source)
    {
        setMinimumSize(300, 200);
        setMouseTracking(true);
        m_mask = QImage(m_source.size(), QImage::Format_Grayscale8);
        m_mask.fill(0);
        m_overlay = QImage(m_source.size(), QImage::Format_ARGB32);
        m_overlay.fill(Qt::transparent);
    }

    // Replaces the current mask wholesale (e.g., when initializing from a
    // saved maskId or synthesizing from a legacy cropRect).
    void setMask(const QImage& mask)
    {
        if (mask.isNull()) {
            m_mask.fill(0);
        }
        else {
            m_mask = mask.convertToFormat(QImage::Format_Grayscale8);
            if (m_mask.size() != m_source.size()) {
                // Defensive: if a mask of the wrong size sneaks in, scale it.
                m_mask =
                    m_mask.scaled(m_source.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
            }
        }
        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
    }

    QImage mask() const
    {
        return m_mask;
    }

    void setTool(Tool t)
    {
        // Cancel any rect drag still in flight - user moved on to a different
        // tool, the half-painted rectangle preview should disappear with them.
        if (m_dragging) {
            m_dragging = false;
            m_currentRect = QRect();
            update();
        }
        m_tool = t;
    }
    void setErase(bool e)
    {
        m_erase = e;
    }
    void setBrushSize(int px)
    {
        m_brushSize = qMax(1, px);
    }
    void setBucketTolerance(int t)
    {
        m_tolerance = qMax(0, t);
    }
    void setTrimMode(bool trim)
    {
        m_trimMode = trim;
        update();
    }

    void clearMask()
    {
        m_mask.fill(0);
        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
    }

    void invertMask()
    {
        if (m_mask.isNull()) return;
        cv::Mat ourMask(m_mask.height(), m_mask.width(), CV_8UC1, m_mask.bits(),
                        m_mask.bytesPerLine());
        cv::bitwise_not(ourMask, ourMask);
        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
    }

    void onMaskChanged(std::function<void()> cb)
    {
        m_onChanged = std::move(cb);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x14, 0x14, 0x14));
        if (m_source.isNull()) return;

        const QRect dst = displayRect();
        p.drawImage(dst, m_source);

        if (m_trimMode) {
            // In trim mode the rect of interest is the mask bbox - dim outside.
            const QRect bbox = maskBoundingBox(m_mask);
            if (!bbox.isEmpty()) {
                const QRect bboxDst = sourceToDisplay(bbox);
                QPainterPath outside;
                outside.addRect(dst);
                outside.addRect(bboxDst);
                outside.setFillRule(Qt::OddEvenFill);
                p.fillPath(outside, QColor(0, 0, 0, 140));
                QPen pen(QColor(220, 220, 220, 220));
                pen.setWidth(1);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.drawRect(bboxDst.adjusted(0, 0, -1, -1));
            }
        }
        else {
            // Mask mode - draw the painted mask as a red translucent overlay.
            p.drawImage(dst, m_overlay);
        }

        // In-progress rect drag - show as a wireframe before commit.
        if (m_tool == Tool::Rect && m_dragging && !m_currentRect.isEmpty()) {
            const QRect rDst = sourceToDisplay(m_currentRect);
            QPen pen(QColor(255, 255, 255, 220));
            pen.setStyle(Qt::DashLine);
            pen.setWidth(1);
            p.setPen(pen);
            p.setBrush(QColor(220, 70, 70, 70));
            p.drawRect(rDst.adjusted(0, 0, -1, -1));
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton || m_source.isNull()) return;
        const QPoint src = displayToSource(e->position().toPoint());

        switch (m_tool) {
        case Tool::Rect:
            m_dragStart = src;
            m_currentRect = QRect(src, src);
            m_dragging = true;
            update();
            break;
        case Tool::Brush:
            m_lastBrushPos = src;
            paintBrushSegment(src, src, m_erase);
            break;
        case Tool::Bucket:
            QApplication::setOverrideCursor(Qt::WaitCursor);
            floodFillAt(src);
            QApplication::restoreOverrideCursor();
            break;
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_source.isNull()) return;
        const QPoint src = displayToSource(e->position().toPoint());

        if (m_tool == Tool::Rect && m_dragging) {
            m_currentRect = QRect(m_dragStart, src).normalized();
            update();
        }
        else if (m_tool == Tool::Brush && (e->buttons() & Qt::LeftButton)) {
            paintBrushSegment(m_lastBrushPos, src, m_erase);
            m_lastBrushPos = src;
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        if (m_tool == Tool::Rect && m_dragging) {
            m_dragging = false;
            const QRect r = m_currentRect.intersected(m_mask.rect());
            m_currentRect = QRect();
            if (r.width() >= 2 && r.height() >= 2) {
                QPainter pm(&m_mask);
                pm.fillRect(r, m_erase ? QColor(0, 0, 0) : QColor(255, 255, 255));
                pm.end();
                rebuildOverlay(r);
                update();
                if (m_onChanged) m_onChanged();
            }
            else {
                update();
            }
        }
    }

private:
    // ── Tool implementations ──

    void paintBrushSegment(QPoint from, QPoint to, bool erase)
    {
        const int radius = m_brushSize / 2 + 1;
        const QRect bbox = QRect(from, to)
                               .normalized()
                               .adjusted(-radius, -radius, radius, radius)
                               .intersected(m_mask.rect());
        if (bbox.isEmpty()) return;

        QPainter pm(&m_mask);
        // Hard edges for now - soft brushes (intermediate alpha) come later.
        pm.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(erase ? QColor(0, 0, 0) : QColor(255, 255, 255));
        pen.setWidth(m_brushSize);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        pm.setPen(pen);
        pm.drawLine(from, to);
        pm.end();

        rebuildOverlay(bbox);
        update(sourceToDisplay(bbox));
        if (m_onChanged) m_onChanged();
    }

    // Tolerance flood fill on the source image's color, OR-ed into m_mask.
    // Uses cv::floodFill with FLOODFILL_MASK_ONLY so the source isn't touched
    // and the result lands in a workspace mask we then merge into ours.
    void floodFillAt(QPoint start)
    {
        if (!m_source.rect().contains(start)) return;

        // cv::floodFill rejects 4-channel images, so drop alpha. RGB888 in Qt
        // is byte-order [R,G,B] per pixel, which CV_8UC3 sees as the same
        // (channel order is irrelevant here - uniform per-channel tolerance).
        QImage rgb = (m_source.format() == QImage::Format_RGB888)
                         ? m_source
                         : m_source.convertToFormat(QImage::Format_RGB888);
        cv::Mat srcMat(rgb.height(), rgb.width(), CV_8UC3, rgb.bits(), rgb.bytesPerLine());

        // OpenCV requires the workspace mask to be 2px larger than the image
        // (1px border on each side acts as a sentinel). Non-zero pixels block
        // the fill, so we start it blank - the existing m_mask is *not*
        // treated as a barrier (matches prior BFS behavior).
        cv::Mat ffMask = cv::Mat::zeros(srcMat.rows + 2, srcMat.cols + 2, CV_8UC1);

        const cv::Scalar lo(m_tolerance, m_tolerance, m_tolerance, m_tolerance);
        const cv::Scalar up = lo;
        // FLOODFILL_MASK_ONLY: don't touch srcMat. Top byte of `flags` is the
        // value written into the mask (default would be 1 - we want 255).
        const int flags = 4 | cv::FLOODFILL_MASK_ONLY | (255 << 8);

        cv::Rect ffBox;
        cv::floodFill(srcMat, ffMask, cv::Point(start.x(), start.y()), cv::Scalar(), &ffBox, lo, up,
                      flags);
        if (ffBox.width <= 0 || ffBox.height <= 0) return;

        // Merge the filled region with m_mask. Add: bitwise OR; erase:
        // saturating subtract so painted pixels become 0. Both ops are
        // cropped to the bbox so cost scales with fill area, not image size.
        cv::Mat ourMask(m_mask.height(), m_mask.width(), CV_8UC1, m_mask.bits(),
                        m_mask.bytesPerLine());
        cv::Mat innerMask = ffMask(cv::Rect(1, 1, srcMat.cols, srcMat.rows));
        cv::Mat dstRoi = ourMask(ffBox);
        cv::Mat srcRoi = innerMask(ffBox);
        if (m_erase)
            cv::subtract(dstRoi, srcRoi, dstRoi);
        else
            cv::bitwise_or(dstRoi, srcRoi, dstRoi);

        const QRect dirty(ffBox.x, ffBox.y, ffBox.width, ffBox.height);
        rebuildOverlay(dirty);
        update();
        if (m_onChanged) m_onChanged();
    }

    // ── Overlay regen ──
    //
    // The overlay is a cached ARGB32 image we drawImage() in paintEvent. It
    // mirrors m_mask but tinted red. Keeping it cached avoids per-paint
    // pixel-walking; we only refresh the bbox of the most recent edit.
    void rebuildOverlay(const QRect& dirtyIn)
    {
        const QRect dirty = dirtyIn.intersected(m_mask.rect());
        if (dirty.isEmpty()) return;
        for (int y = dirty.top(); y <= dirty.bottom(); ++y) {
            QRgb* outRow = reinterpret_cast<QRgb*>(m_overlay.scanLine(y));
            const uchar* maskRow = m_mask.constScanLine(y);
            for (int x = dirty.left(); x <= dirty.right(); ++x) {
                const int v = maskRow[x];
                outRow[x] = v ? qRgba(220, 70, 70, 110) : 0;
            }
        }
    }

    // ── Geometry helpers ──

    QRect displayRect() const
    {
        if (m_source.isNull()) return rect();
        const QSize fitted = m_source.size().scaled(size(), Qt::KeepAspectRatio);
        return QRect(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2),
                     fitted);
    }

    QPoint displayToSource(QPoint p) const
    {
        const QRect dst = displayRect();
        if (dst.isEmpty()) return {};
        const double sx = double(m_source.width()) / dst.width();
        const double sy = double(m_source.height()) / dst.height();
        const int x = qBound(0, int((p.x() - dst.left()) * sx), m_source.width() - 1);
        const int y = qBound(0, int((p.y() - dst.top()) * sy), m_source.height() - 1);
        return {x, y};
    }

    QRect sourceToDisplay(QRect r) const
    {
        const QRect dst = displayRect();
        if (dst.isEmpty() || m_source.isNull()) return {};
        const double sx = double(dst.width()) / m_source.width();
        const double sy = double(dst.height()) / m_source.height();
        return QRect(dst.left() + int(r.left() * sx), dst.top() + int(r.top() * sy),
                     qMax(1, int(r.width() * sx)), qMax(1, int(r.height() * sy)));
    }

    QImage m_source;
    QImage m_mask;    // Grayscale8, source-sized; 0 = clear, 255 = mask
    QImage m_overlay; // ARGB32, source-sized; cached red tint of m_mask

    Tool m_tool = Tool::Brush;
    bool m_erase = false; // modifier - flips Add/Remove for any tool
    int m_brushSize = 30;
    int m_tolerance = 16;
    bool m_trimMode = false;

    QPoint m_dragStart;
    QRect m_currentRect;
    bool m_dragging = false;
    QPoint m_lastBrushPos;

    std::function<void()> m_onChanged;
};

// ── Dialog ──────────────────────────────────────────────────────────────────

ClipEditorDialog::ClipEditorDialog(const QImage& source, const core::ImageEdits& initial,
                                   core::WorkflowInputCache* cache, QWidget* parent)
    : ChromedDialog(parent), m_cache(cache)
{
    setWindowTitle("Clip Editor");
    // Floor below which the editor's controls would clip - overrides
    // ChromedDialog::computeResizeGeometry's 320×200 default.
    setMinimumSize(640, 480);
    resize(900, 760);

    m_canvas = new ClipCanvas(source, this);

    // Initialize mask: prefer a saved maskId; fall back to a synthesized
    // rect-mask for legacy edits; otherwise blank.
    if (m_cache && !initial.maskId.isEmpty()) {
        m_canvas->setMask(m_cache->loadMask(initial.maskId));
    }
    else if (initial.enabled && !initial.cropRect.isEmpty()) {
        QImage seed(source.size(), QImage::Format_Grayscale8);
        seed.fill(0);
        QPainter p(&seed);
        p.fillRect(initial.cropRect.intersected(seed.rect()), QColor(255, 255, 255));
        p.end();
        m_canvas->setMask(seed);
    }
    m_canvas->setTrimMode(initial.trimToCrop);
    m_canvas->onMaskChanged([this]() { updateRectLabel(); });

    // ── Tool palette ──
    m_toolGroup = new QButtonGroup(this);
    auto makeToolBtn = [this](const QString& text, Tool t, bool checked = false) {
        auto* b = new QPushButton(text, this);
        b->setObjectName("ClipEditorToolBtn");
        b->setCheckable(true);
        b->setChecked(checked);
        b->setCursor(Qt::PointingHandCursor);
        m_toolGroup->addButton(b, int(t));
        connect(b, &QPushButton::clicked, this, [this, t]() {
            m_canvas->setTool(t);
            updateToolControls();
        });
        return b;
    };
    auto* rectBtn = makeToolBtn("Rect", Tool::Rect);
    auto* brushBtn = makeToolBtn("Brush", Tool::Brush, true); // default
    auto* bucketBtn = makeToolBtn("Bucket", Tool::Bucket);
    m_canvas->setTool(Tool::Brush);

    // ── Erase modifier - combines with whichever tool is active ──
    m_eraseBtn = new QPushButton("Erase", this);
    m_eraseBtn->setObjectName("ClipEditorEraseBtn");
    m_eraseBtn->setCheckable(true);
    m_eraseBtn->setCursor(Qt::PointingHandCursor);
    m_eraseBtn->setToolTip("When on, the active tool removes from the mask "
                           "instead of adding to it.");
    connect(m_eraseBtn, &QPushButton::toggled, this, [this](bool on) { m_canvas->setErase(on); });

    // ── Sliders ──
    m_brushLabel = new QLabel("Size 30", this);
    m_brushSize = new QSlider(Qt::Horizontal, this);
    m_brushSize->setRange(1, 200);
    m_brushSize->setValue(30);
    m_brushSize->setFixedWidth(150);
    connect(m_brushSize, &QSlider::valueChanged, this, [this](int v) {
        m_canvas->setBrushSize(v);
        m_brushLabel->setText(QStringLiteral("Size %1").arg(v));
    });

    m_tolLabel = new QLabel("Tol 16", this);
    m_tolerance = new QSlider(Qt::Horizontal, this);
    m_tolerance->setRange(0, 255);
    m_tolerance->setValue(16);
    m_tolerance->setFixedWidth(150);
    connect(m_tolerance, &QSlider::valueChanged, this, [this](int v) {
        m_canvas->setBucketTolerance(v);
        m_tolLabel->setText(QStringLiteral("Tol %1").arg(v));
    });

    // ── Bottom bar ──
    m_rectLabel = new QLabel(this);
    m_rectLabel->setObjectName("ClipEditorRectLabel");

    m_trimToCrop = new QCheckBox("Trim canvas to crop (no mask)", this);
    m_trimToCrop->setChecked(initial.trimToCrop);
    m_trimToCrop->setToolTip("Off: painted mask defines MASK. Output is source-sized; LoadImage's "
                             "IMAGE = original picture, MASK = 1 inside the painted region.\n"
                             "On: rect-only crop. Output is the rect cropped from source; "
                             "alpha=255 everywhere (no mask).");
    connect(m_trimToCrop, &QCheckBox::toggled, this, [this](bool on) {
        // User-initiated toggle to trim mode discards any painted mask -
        // trim mode is conceptually "just a crop, no mask," so retaining
        // mask data would be misleading.
        if (on) m_canvas->clearMask();
        applyTrimModeUI(on);
    });

    auto* clearBtn = new QPushButton("Clear mask", this);
    connect(clearBtn, &QPushButton::clicked, this, [this]() { m_canvas->clearMask(); });

    m_invertBtn = new QPushButton("Invert mask", this);
    m_invertBtn->setToolTip("Flip every mask pixel - what was selected becomes unselected and "
                            "vice versa. Useful when it's easier to paint the keep region than "
                            "the mask region.");
    connect(m_invertBtn, &QPushButton::clicked, this, [this]() { m_canvas->invertMask(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Apply");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ── Layout ──
    auto* toolRow = new QHBoxLayout;
    toolRow->setSpacing(6);
    toolRow->addWidget(rectBtn);
    toolRow->addWidget(brushBtn);
    toolRow->addWidget(bucketBtn);
    toolRow->addSpacing(12);
    toolRow->addWidget(m_eraseBtn);
    toolRow->addSpacing(16);
    toolRow->addWidget(m_brushLabel);
    toolRow->addWidget(m_brushSize);
    toolRow->addSpacing(16);
    toolRow->addWidget(m_tolLabel);
    toolRow->addWidget(m_tolerance);
    toolRow->addStretch();

    auto* footRow = new QHBoxLayout;
    footRow->setSpacing(12);
    footRow->addWidget(clearBtn);
    footRow->addWidget(m_invertBtn);
    footRow->addWidget(m_trimToCrop);
    footRow->addWidget(m_rectLabel, 1);

    // Editor content lives inside ChromedDialog's contentArea(); the
    // titlebar + cosmetic border + edge-resize chrome are owned by the base
    // class.
    auto* contentLayout = new QVBoxLayout(contentArea());
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(8);
    contentLayout->addLayout(toolRow);
    contentLayout->addWidget(m_canvas, 1);
    contentLayout->addLayout(footRow);
    contentLayout->addWidget(buttons);

    // Initial palette state - applies whatever trim mode the var was saved
    // with. Doesn't clear the mask on initial setup (only the user toggle
    // does that, so legacy data with both a mask and trimToCrop=true loads
    // intact and can be examined).
    applyTrimModeUI(initial.trimToCrop);
}

void ClipEditorDialog::accept()
{
    // Translate canvas state into the result ImageEdits the caller will
    // adopt. Empty mask → disabled (caller drops the maskId / clears edits).
    const QImage finalMask = m_canvas->mask();
    const QRect bbox = maskBoundingBox(finalMask);

    if (bbox.isEmpty()) {
        m_result = core::ImageEdits{};
        QDialog::accept();
        return;
    }

    m_result.enabled = true;
    m_result.cropRect = bbox;
    m_result.trimToCrop = m_trimToCrop->isChecked();
    // Trim mode discards the mask at render time, so don't persist one - saves
    // a cache file and keeps the var card label honest ("cropped W×H" rather
    // than implying a mask is involved).
    m_result.maskId = (!m_result.trimToCrop && m_cache) ? m_cache->saveMask(finalMask) : QString();
    QDialog::accept();
}

void ClipEditorDialog::updateRectLabel()
{
    const QRect r = maskBoundingBox(m_canvas->mask());
    if (r.isEmpty()) {
        m_rectLabel->setText("No mask painted - output will be the source unchanged.");
        return;
    }
    const QString label = m_trimToCrop->isChecked()
                              ? QStringLiteral("Crop bbox: x=%1 y=%2  %3 × %4")
                              : QStringLiteral("Mask bbox: x=%1 y=%2  %3 × %4");
    m_rectLabel->setText(label.arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
}

void ClipEditorDialog::updateToolControls()
{
    // Visual hint: the size slider only affects brush, the tolerance slider
    // only affects bucket. Disable irrelevant controls so the active tool's
    // parameters are obvious.
    const int id = m_toolGroup->checkedId();
    const bool isBrush = (id == int(Tool::Brush));
    const bool isBucket = (id == int(Tool::Bucket));
    m_brushSize->setEnabled(isBrush);
    m_brushLabel->setEnabled(isBrush);
    m_tolerance->setEnabled(isBucket);
    m_tolLabel->setEnabled(isBucket);
}

void ClipEditorDialog::applyTrimModeUI(bool on)
{
    m_canvas->setTrimMode(on);

    if (on) {
        // Force Rect - Brush/Bucket paint into a mask that trim mode ignores,
        // so they'd be silently no-ops. Better to lock them out.
        if (auto* rectBtn = m_toolGroup->button(int(Tool::Rect))) rectBtn->setChecked(true);
        m_canvas->setTool(Tool::Rect);
        // Erase has nothing meaningful to do in trim mode either.
        m_eraseBtn->setChecked(false);
        m_canvas->setErase(false);
    }
    if (auto* b = m_toolGroup->button(int(Tool::Brush))) b->setEnabled(!on);
    if (auto* b = m_toolGroup->button(int(Tool::Bucket))) b->setEnabled(!on);
    m_eraseBtn->setEnabled(!on);
    if (m_invertBtn) m_invertBtn->setEnabled(!on);

    updateToolControls();
    updateRectLabel();
}

} // namespace gui
