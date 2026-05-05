#include <gui/composer/clipeditordialog.h>
#include <core/workflowinputcache.h>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStack>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <cmath>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace gui {

namespace {

// Eraser is a modifier, not its own tool - applied as Rect+erase / Brush+
// erase / Bucket+erase / MaskFill+erase to clear instead of paint.
enum class Tool { Rect, Brush, Bucket, MaskFill };

// Tight bounding box of non-zero pixels in a Grayscale8 mask; null QRect
// when the mask is all-zero.
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

    // Replaces the current mask wholesale, e.g. on init from a saved
    // maskId or a synthesized legacy cropRect.
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
        // Cancel any in-flight rect drag so the half-painted preview
        // doesn't linger across a tool change.
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
        pushUndo();
        m_mask.fill(0);
        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
    }

    void invertMask()
    {
        if (m_mask.isNull()) return;
        pushUndo();
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

    // Overlay tint - session-only, not persisted. Default matches the app's
    // accent green (#66aa66) at the original alpha (110/255).
    QColor overlayColor() const { return m_overlayColor; }
    int overlayAlpha() const { return m_overlayAlpha; }

    void setOverlayColor(const QColor& c)
    {
        if (!c.isValid()) return;
        m_overlayColor = QColor(c.red(), c.green(), c.blue());
        rebuildOverlay(m_mask.rect());
        update();
    }

    void setOverlayAlpha(int a)
    {
        m_overlayAlpha = qBound(0, a, 255);
        rebuildOverlay(m_mask.rect());
        update();
    }

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    void undo()
    {
        if (m_undoStack.isEmpty()) return;
        m_redoStack.push(m_mask);
        m_mask = m_undoStack.pop();
        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
        if (m_onHistoryChanged) m_onHistoryChanged();
    }

    void redo()
    {
        if (m_redoStack.isEmpty()) return;
        m_undoStack.push(m_mask);
        m_mask = m_redoStack.pop();
        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
        if (m_onHistoryChanged) m_onHistoryChanged();
    }

    void onHistoryChanged(std::function<void()> cb)
    {
        m_onHistoryChanged = std::move(cb);
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
            // The mask bbox is the rect of interest; dim everything outside.
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
            p.drawImage(dst, m_overlay);
        }

        // Wireframe preview while a rect drag is in flight.
        if (m_tool == Tool::Rect && m_dragging && !m_currentRect.isEmpty()) {
            const QRect rDst = sourceToDisplay(m_currentRect);
            QPen pen(QColor(255, 255, 255, 220));
            pen.setStyle(Qt::DashLine);
            pen.setWidth(1);
            p.setPen(pen);
            QColor fill = m_overlayColor;
            fill.setAlpha(70);
            p.setBrush(fill);
            p.drawRect(rDst.adjusted(0, 0, -1, -1));
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton || m_source.isNull()) return;
        const QPoint p = e->position().toPoint();

        // Click in the empty margin around the image starts a pan drag, so
        // tools never fire from a clamped-to-edge source pixel.
        if (!displayRect().contains(p)) {
            m_panning = true;
            m_panStart = p;
            setCursor(Qt::ClosedHandCursor);
            return;
        }

        const QPoint src = displayToSource(p);
        switch (m_tool) {
        case Tool::Rect:
            m_dragStart = src;
            m_currentRect = QRect(src, src);
            m_dragging = true;
            update();
            break;
        case Tool::Brush:
            pushUndo();
            m_lastBrushPos = src;
            paintBrushSegment(src, src, m_erase);
            break;
        case Tool::Bucket:
            pushUndo();
            QApplication::setOverrideCursor(Qt::WaitCursor);
            floodFillAt(src);
            QApplication::restoreOverrideCursor();
            break;
        case Tool::MaskFill:
            // pushUndo lives in maskFillAt so a no-op click on an already-
            // filled (or already-empty) region doesn't waste a history slot.
            QApplication::setOverrideCursor(Qt::WaitCursor);
            maskFillAt(src);
            QApplication::restoreOverrideCursor();
            break;
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_source.isNull()) return;
        const QPoint p = e->position().toPoint();

        if (m_panning) {
            m_pan += p - m_panStart;
            m_panStart = p;
            update();
            return;
        }

        const QPoint src = displayToSource(p);
        const bool overImage = displayRect().contains(p);

        if (m_tool == Tool::Rect && m_dragging) {
            m_currentRect = QRect(m_dragStart, src).normalized();
            update();
        }
        else if (m_tool == Tool::Brush && (e->buttons() & Qt::LeftButton) && overImage) {
            paintBrushSegment(m_lastBrushPos, src, m_erase);
            m_lastBrushPos = src;
        }

        // Hover cursor hint while no button is held.
        if (!(e->buttons() & Qt::LeftButton))
            setCursor(overImage ? Qt::ArrowCursor : Qt::OpenHandCursor);
    }

    // Cursor-anchored zoom: the source pixel under the cursor stays put as
    // m_zoom changes; we shift m_pan to compensate.
    void wheelEvent(QWheelEvent* e) override
    {
        if (m_source.isNull()) {
            e->ignore();
            return;
        }
        const QPointF cursor = e->position();
        const QPointF srcAtCursor = displayToSourceF(cursor);

        const double oldZoom = m_zoom;
        const double steps = e->angleDelta().y() / 120.0;
        m_zoom = qBound(kMinZoom, m_zoom * std::pow(1.15, steps), kMaxZoom);
        if (qFuzzyCompare(m_zoom, oldZoom)) {
            e->accept();
            return;
        }

        const QPointF newPos = sourceToDisplayF(srcAtCursor);
        const QPointF delta = cursor - newPos;
        m_pan += QPoint(int(delta.x()), int(delta.y()));

        update();
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        if (m_panning) {
            m_panning = false;
            const QPoint p = e->position().toPoint();
            setCursor(displayRect().contains(p) ? Qt::ArrowCursor : Qt::OpenHandCursor);
            return;
        }
        if (m_tool == Tool::Rect && m_dragging) {
            m_dragging = false;
            const QRect r = m_currentRect.intersected(m_mask.rect());
            m_currentRect = QRect();
            if (r.width() >= 2 && r.height() >= 2) {
                pushUndo();
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

    // Tolerance flood fill into a workspace mask via FLOODFILL_MASK_ONLY,
    // then OR (or saturating-subtract for erase) into m_mask.
    void floodFillAt(QPoint start)
    {
        if (!m_source.rect().contains(start)) return;

        // floodFill rejects 4-channel images; drop alpha. Channel order
        // doesn't matter for uniform per-channel tolerance.
        QImage rgb = (m_source.format() == QImage::Format_RGB888)
                         ? m_source
                         : m_source.convertToFormat(QImage::Format_RGB888);
        cv::Mat srcMat(rgb.height(), rgb.width(), CV_8UC3, rgb.bits(), rgb.bytesPerLine());

        // OpenCV requires the workspace mask to be 2px larger (1px border
        // sentinel). Starts blank so m_mask doesn't act as a fill barrier.
        cv::Mat ffMask = cv::Mat::zeros(srcMat.rows + 2, srcMat.cols + 2, CV_8UC1);

        const cv::Scalar lo(m_tolerance, m_tolerance, m_tolerance, m_tolerance);
        const cv::Scalar up = lo;
        // Top byte of flags is the value written into the mask (default 1).
        const int flags = 4 | cv::FLOODFILL_MASK_ONLY | (255 << 8);

        cv::Rect ffBox;
        cv::floodFill(srcMat, ffMask, cv::Point(start.x(), start.y()), cv::Scalar(), &ffBox, lo, up,
                      flags);
        if (ffBox.width <= 0 || ffBox.height <= 0) return;

        // Both ops cropped to the bbox so cost scales with fill area.
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

    // Mask-only flood: fills connected pixels of the seed's current value
    // with its inverse, ignoring the source image. Stops at the painted mask
    // boundary or the image edges. Erase modifier flips which side is filled.
    void maskFillAt(QPoint start)
    {
        if (!m_mask.rect().contains(start)) return;
        const uchar seed = m_mask.constScanLine(start.y())[start.x()];
        const uchar expected = m_erase ? 255 : 0;
        if (seed != expected) return; // already in the desired state

        pushUndo();
        const uchar newVal = m_erase ? 0 : 255;
        cv::Mat m(m_mask.height(), m_mask.width(), CV_8UC1, m_mask.bits(),
                  m_mask.bytesPerLine());
        cv::Rect ffBox;
        cv::floodFill(m, cv::Point(start.x(), start.y()), cv::Scalar(newVal), &ffBox,
                      cv::Scalar(0), cv::Scalar(0), 4);
        if (ffBox.width <= 0 || ffBox.height <= 0) return;
        const QRect dirty(ffBox.x, ffBox.y, ffBox.width, ffBox.height);
        rebuildOverlay(dirty);
        update();
        if (m_onChanged) m_onChanged();
    }

    // ── Overlay regen ──
    // Cached red-tinted mirror of m_mask, blitted in paintEvent. Refreshed
    // only over the dirty bbox so we don't pixel-walk the whole image per
    // edit.
    void rebuildOverlay(const QRect& dirtyIn)
    {
        const QRect dirty = dirtyIn.intersected(m_mask.rect());
        if (dirty.isEmpty()) return;
        const QRgb tint = qRgba(m_overlayColor.red(), m_overlayColor.green(),
                                m_overlayColor.blue(), m_overlayAlpha);
        for (int y = dirty.top(); y <= dirty.bottom(); ++y) {
            QRgb* outRow = reinterpret_cast<QRgb*>(m_overlay.scanLine(y));
            const uchar* maskRow = m_mask.constScanLine(y);
            for (int x = dirty.left(); x <= dirty.right(); ++x) {
                const int v = maskRow[x];
                outRow[x] = v ? tint : 0;
            }
        }
    }

    // ── Geometry helpers ──

    QRect displayRect() const
    {
        if (m_source.isNull()) return rect();
        const QSize fitted = m_source.size().scaled(size(), Qt::KeepAspectRatio);
        const QSize scaled(qMax(1, int(fitted.width() * m_zoom)),
                           qMax(1, int(fitted.height() * m_zoom)));
        const QPoint center((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
        return QRect(center + m_pan, scaled);
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

    // Float versions used by the zoom anchoring math; need sub-pixel accuracy
    // so cursor-anchored zoom doesn't drift over many wheel ticks.
    QPointF displayToSourceF(QPointF p) const
    {
        const QRect dst = displayRect();
        if (dst.isEmpty()) return {};
        const double sx = double(m_source.width()) / dst.width();
        const double sy = double(m_source.height()) / dst.height();
        return QPointF((p.x() - dst.left()) * sx, (p.y() - dst.top()) * sy);
    }

    QPointF sourceToDisplayF(QPointF p) const
    {
        const QRect dst = displayRect();
        if (dst.isEmpty() || m_source.isNull()) return {};
        const double sx = double(dst.width()) / m_source.width();
        const double sy = double(dst.height()) / m_source.height();
        return QPointF(dst.left() + p.x() * sx, dst.top() + p.y() * sy);
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

    // Snapshot the mask before a mutating action; clears redo.
    void pushUndo()
    {
        m_undoStack.push(m_mask);
        while (m_undoStack.size() > kMaxHistory) m_undoStack.removeFirst();
        m_redoStack.clear();
        if (m_onHistoryChanged) m_onHistoryChanged();
    }

    static constexpr int kMaxHistory = 32;
    static constexpr double kMinZoom = 0.5;
    static constexpr double kMaxZoom = 64.0;

    QImage m_source;
    QImage m_mask;    // Grayscale8, source-sized; 0 = clear, 255 = mask
    QImage m_overlay; // ARGB32, source-sized; cached red tint of m_mask

    Tool m_tool = Tool::Brush;
    bool m_erase = false; // modifier - flips Add/Remove for any tool
    int m_brushSize = 30;
    int m_tolerance = 16;
    bool m_trimMode = false;

    QColor m_overlayColor{102, 170, 102}; // #66aa66
    int m_overlayAlpha = 110;

    QPoint m_dragStart;
    QRect m_currentRect;
    bool m_dragging = false;
    QPoint m_lastBrushPos;

    bool m_panning = false;
    QPoint m_panStart;

    // m_pan offsets the display rect from the centred fit position; updated
    // by wheel zoom (cursor anchor) and by margin click-drag.
    double m_zoom = 1.0;
    QPoint m_pan;

    QStack<QImage> m_undoStack;
    QStack<QImage> m_redoStack;

    std::function<void()> m_onChanged;
    std::function<void()> m_onHistoryChanged;
};

// ── Dialog ──────────────────────────────────────────────────────────────────

ClipEditorDialog::ClipEditorDialog(const QImage& source, const core::ImageEdits& initial,
                                   core::WorkflowInputCache* cache, QWidget* parent)
    : ChromedDialog(parent), m_cache(cache)
{
    setWindowTitle("Clip Editor");
    setMinimumSize(920, 920);
    resize(1024, 920);

    // Green slider + tool-button styling. Tool buttons override the dialog-
    // wide red checked state from app.qss - tools are a selector, not a
    // destructive toggle. Erase keeps the red.
    setStyleSheet(
        "QSlider::groove:horizontal { background: #1a1a1a; height: 4px;"
        " border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #336633; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #66aa66; border: 1px solid #336633;"
        " width: 12px; margin: -5px 0; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { background: #77bb77; }"
        "QPushButton#ClipEditorToolBtn:checked { background: #182418; color: #5a9a5a;"
        " border-color: #2a4a2a; }"
        "QPushButton#ClipEditorToolBtn:checked:hover { background: #1e2e1e;"
        " color: #7acc7a; border-color: #3a6a3a; }");

    m_canvas = new ClipCanvas(source, this);

    // Prefer saved maskId; fall back to a rect-mask for legacy edits.
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
    auto* maskFillBtn = makeToolBtn("Mask Fill", Tool::MaskFill);
    bucketBtn->setToolTip("Flood fill source-similar pixels into the mask.");
    maskFillBtn->setToolTip("Flood fill the empty mask region until the painted mask "
                            "or image edge contains it. With Erase, clears a contained mask region.");
    m_canvas->setTool(Tool::Brush);

    // ── Undo / redo ──
    auto* undoBtn = new QPushButton("Undo", this);
    auto* redoBtn = new QPushButton("Redo", this);
    undoBtn->setEnabled(false);
    redoBtn->setEnabled(false);
    undoBtn->setToolTip("Undo (Ctrl+Z)");
    redoBtn->setToolTip("Redo (Ctrl+Y)");
    connect(undoBtn, &QPushButton::clicked, this, [this]() { m_canvas->undo(); });
    connect(redoBtn, &QPushButton::clicked, this, [this]() { m_canvas->redo(); });
    m_canvas->onHistoryChanged([this, undoBtn, redoBtn]() {
        undoBtn->setEnabled(m_canvas->canUndo());
        redoBtn->setEnabled(m_canvas->canRedo());
    });

    auto* undoSc = new QShortcut(QKeySequence::Undo, this);
    auto* redoSc = new QShortcut(QKeySequence::Redo, this);
    connect(undoSc, &QShortcut::activated, this, [this]() { m_canvas->undo(); });
    connect(redoSc, &QShortcut::activated, this, [this]() { m_canvas->redo(); });

    // ── Erase modifier - combines with whichever tool is active ──
    m_eraseBtn = new QPushButton("Erase", this);
    m_eraseBtn->setObjectName("ClipEditorEraseBtn");
    m_eraseBtn->setCheckable(true);
    m_eraseBtn->setCursor(Qt::PointingHandCursor);
    m_eraseBtn->setToolTip("When on, the active tool removes from the mask "
                           "instead of adding to it.");
    connect(m_eraseBtn, &QPushButton::toggled, this, [this](bool on) { m_canvas->setErase(on); });

    // ── Sliders ──
    // Fixed label widths so valueChanged doesn't reshuffle the row mid-drag.
    m_brushLabel = new QLabel("Size 30", this);
    m_brushLabel->setFixedWidth(m_brushLabel->fontMetrics().horizontalAdvance("Size 200") + 24);
    m_brushSize = new QSlider(Qt::Horizontal, this);
    m_brushSize->setRange(1, 200);
    m_brushSize->setValue(30);
    connect(m_brushSize, &QSlider::valueChanged, this, [this](int v) {
        m_canvas->setBrushSize(v);
        m_brushLabel->setText(QStringLiteral("Size %1").arg(v));
    });

    m_tolLabel = new QLabel("Tolerance 16", this);
    m_tolLabel->setFixedWidth(m_tolLabel->fontMetrics().horizontalAdvance("Tolerance 255") + 48);
    m_tolerance = new QSlider(Qt::Horizontal, this);
    m_tolerance->setRange(0, 255);
    m_tolerance->setValue(16);
    connect(m_tolerance, &QSlider::valueChanged, this, [this](int v) {
        m_canvas->setBucketTolerance(v);
        m_tolLabel->setText(QStringLiteral("Tolerance %1").arg(v));
    });

    // ── Overlay tint ──
    auto* colorBtn = new QPushButton(this);
    colorBtn->setFixedSize(24, 24);
    colorBtn->setCursor(Qt::PointingHandCursor);
    colorBtn->setToolTip("Overlay color");
    auto applySwatch = [this, colorBtn]() {
        colorBtn->setStyleSheet(
            QString("background: %1; border: 1px solid #2a2a2a; border-radius: 3px;")
                .arg(m_canvas->overlayColor().name()));
    };
    applySwatch();
    connect(colorBtn, &QPushButton::clicked, this, [this, applySwatch]() {
        // Wrap the picker in a ChromedDialog so it gets the app's title bar.
        ChromedDialog wrapper(this);
        wrapper.setWindowTitle("Overlay color");

        auto* picker = new QColorDialog(m_canvas->overlayColor(), wrapper.contentArea());
        picker->setOptions(QColorDialog::DontUseNativeDialog | QColorDialog::NoButtons);
        picker->setWindowFlags(Qt::Widget); // embed as child, not top-level
        picker->setSizeGripEnabled(false);

        auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                          wrapper.contentArea());
        connect(btns, &QDialogButtonBox::accepted, &wrapper, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &wrapper, &QDialog::reject);
        connect(picker, &QColorDialog::colorSelected, &wrapper,
                [&wrapper](const QColor&) { wrapper.accept(); });
        // Esc on the embedded picker calls QDialog::done(Rejected) which
        // only hides the picker; forward to the wrapper so it closes too.
        connect(picker, &QDialog::rejected, &wrapper, &QDialog::reject);

        auto* layout = new QVBoxLayout(wrapper.contentArea());
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(picker, 1);
        layout->addWidget(btns);

        if (wrapper.exec() != QDialog::Accepted) return;
        const QColor chosen = picker->currentColor();
        if (!chosen.isValid()) return;
        m_canvas->setOverlayColor(chosen);
        applySwatch();
    });

    auto* opLabel = new QLabel("Opacity 110", this);
    opLabel->setFixedWidth(opLabel->fontMetrics().horizontalAdvance("Opacity 255") + 48);
    auto* opSlider = new QSlider(Qt::Horizontal, this);
    opSlider->setRange(0, 255);
    opSlider->setValue(110);
    connect(opSlider, &QSlider::valueChanged, this, [this, opLabel](int v) {
        m_canvas->setOverlayAlpha(v);
        opLabel->setText(QStringLiteral("Opacity %1").arg(v));
    });

    // ── Bottom bar ──
    m_rectLabel = new QLabel(this);
    m_rectLabel->setObjectName("ClipEditorRectLabel");

    m_trimToCrop = new QCheckBox("Crop only (no mask)", this);
    m_trimToCrop->setChecked(initial.trimToCrop);
    m_trimToCrop->setToolTip("Off: painted mask defines MASK. Output is source-sized; LoadImage's "
                             "IMAGE = original picture, MASK = 1 inside the painted region.\n"
                             "On: rect-only crop. Output is the rect cropped from source; "
                             "alpha=255 everywhere (no mask).");
    connect(m_trimToCrop, &QCheckBox::toggled, this, [this](bool on) {
        // Trim mode is "just a crop"; retaining mask data would be misleading.
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

    // ── Layout: sidebar of controls + canvas pane ──
    auto* sidebar = new QWidget(this);
    sidebar->setFixedWidth(220);
    auto* sb = new QVBoxLayout(sidebar);
    sb->setContentsMargins(0, 0, 0, 0);
    sb->setSpacing(6);

    // Tool palette: 2x2 grid keeps the four buttons compact.
    auto* toolGrid = new QGridLayout;
    toolGrid->setSpacing(6);
    toolGrid->addWidget(rectBtn, 0, 0);
    toolGrid->addWidget(brushBtn, 0, 1);
    toolGrid->addWidget(bucketBtn, 1, 0);
    toolGrid->addWidget(maskFillBtn, 1, 1);
    sb->addLayout(toolGrid);

    sb->addWidget(m_eraseBtn);

    auto* historyRow = new QHBoxLayout;
    historyRow->setSpacing(6);
    historyRow->addWidget(undoBtn);
    historyRow->addWidget(redoBtn);
    sb->addLayout(historyRow);

    sb->addSpacing(8);
    sb->addWidget(m_brushLabel);
    sb->addWidget(m_brushSize);

    sb->addSpacing(4);
    sb->addWidget(m_tolLabel);
    sb->addWidget(m_tolerance);

    sb->addSpacing(4);
    auto* colorRow = new QHBoxLayout;
    colorRow->setSpacing(8);
    colorRow->addWidget(colorBtn);
    colorRow->addWidget(opLabel);
    colorRow->addStretch();
    sb->addLayout(colorRow);
    sb->addWidget(opSlider);

    sb->addSpacing(12);
    sb->addWidget(clearBtn);
    sb->addWidget(m_invertBtn);
    sb->addWidget(m_trimToCrop);

    sb->addStretch();

    auto* split = new QHBoxLayout;
    split->setSpacing(8);
    split->addWidget(sidebar);
    split->addWidget(m_canvas, 1);

    // rectLabel is the status read-out; pin it to the bottom row alongside
    // the dialog buttons so it stays visible without claiming sidebar space.
    m_rectLabel->setWordWrap(true);
    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(8);
    statusRow->addWidget(m_rectLabel, 1);
    statusRow->addWidget(buttons);

    auto* contentLayout = new QVBoxLayout(contentArea());
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(8);
    contentLayout->addLayout(split, 1);
    contentLayout->addLayout(statusRow);

    // Apply saved trim mode without clearing - legacy data with both a
    // mask and trimToCrop=true should load intact.
    applyTrimModeUI(initial.trimToCrop);
}

void ClipEditorDialog::accept()
{
    // Translate canvas state into the result ImageEdits the caller adopts;
    // an empty mask means disabled (caller drops maskId / clears edits).
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
    // Trim mode discards the mask at render time; not persisting one
    // saves a cache file and keeps the var card label as "cropped WxH"
    // rather than implying a mask is involved.
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
    // Disable controls that don't apply to the active tool so its
    // parameters are visually obvious.
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
        // Trim mode ignores the mask, so Brush/Bucket/Erase would silently
        // no-op; force Rect and lock the rest out.
        if (auto* rectBtn = m_toolGroup->button(int(Tool::Rect))) rectBtn->setChecked(true);
        m_canvas->setTool(Tool::Rect);
        m_eraseBtn->setChecked(false);
        m_canvas->setErase(false);
    }
    if (auto* b = m_toolGroup->button(int(Tool::Brush))) b->setEnabled(!on);
    if (auto* b = m_toolGroup->button(int(Tool::Bucket))) b->setEnabled(!on);
    if (auto* b = m_toolGroup->button(int(Tool::MaskFill))) b->setEnabled(!on);
    m_eraseBtn->setEnabled(!on);
    if (m_invertBtn) m_invertBtn->setEnabled(!on);

    updateToolControls();
    updateRectLabel();
}

} // namespace gui
