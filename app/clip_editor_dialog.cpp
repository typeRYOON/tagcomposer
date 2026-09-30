#include <app/clip_editor_dialog.h>
#include <app/workflow_input_cache.h>
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
#include <functional>
#include <vector>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// Erase is a modifier rather than its own tool: Rect+erase, Brush+erase and
// so on clear instead of paint.
enum class Tool { Rect, Brush, Bucket, MaskFill };

// The tight box around every non-zero pixel, or a null rect when the mask is
// entirely empty.
QRect maskBoundingBox(const QImage& mask)
{
    if (mask.isNull()) return {};

    int minX = mask.width();
    int minY = mask.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (!row[x]) continue;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < 0) return {};
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

// A four-connected scanline flood fill.
//
// `matches` decides whether a pixel belongs to the region and `visit` marks
// it. Both take image coordinates. The filled bounding box comes back, or a
// null rect when nothing was filled.
//
// Scanline rather than per-pixel recursion: a fill over a large flat region
// is thousands of pixels deep, which a naive version would stack-overflow on.
QRect scanlineFill(const QSize& size, QPoint start,
                   const std::function<bool(int, int)>& matches,
                   const std::function<void(int, int)>& visit)
{
    if (!QRect(QPoint(0, 0), size).contains(start)) return {};
    if (!matches(start.x(), start.y())) return {};

    const int width = size.width();
    const int height = size.height();

    // Visited is tracked separately, because `visit` may write a value the
    // `matches` test still accepts.
    std::vector<bool> seen(size_t(width) * size_t(height), false);
    const auto index = [width](int x, int y) { return size_t(y) * size_t(width) + size_t(x); };

    int minX = start.x();
    int maxX = start.x();
    int minY = start.y();
    int maxY = start.y();

    std::vector<QPoint> stack;
    stack.push_back(start);

    while (!stack.empty()) {
        const QPoint point = stack.back();
        stack.pop_back();

        const int y = point.y();
        if (seen[index(point.x(), y)]) continue;

        // Walk left and right to the ends of this run.
        int left = point.x();
        while (left > 0 && !seen[index(left - 1, y)] && matches(left - 1, y))
            --left;

        int right = point.x();
        while (right < width - 1 && !seen[index(right + 1, y)] && matches(right + 1, y))
            ++right;

        for (int x = left; x <= right; ++x) {
            seen[index(x, y)] = true;
            visit(x, y);
        }

        minX = std::min(minX, left);
        maxX = std::max(maxX, right);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);

        // Seed the rows above and below, one point per contiguous run so the
        // stack stays proportional to the region's perimeter.
        for (int neighbourY : {y - 1, y + 1}) {
            if (neighbourY < 0 || neighbourY >= height) continue;

            bool inRun = false;
            for (int x = left; x <= right; ++x) {
                const bool usable = !seen[index(x, neighbourY)] && matches(x, neighbourY);
                if (usable && !inRun) {
                    stack.push_back(QPoint(x, neighbourY));
                    inRun = true;
                } else if (!usable) {
                    inRun = false;
                }
            }
        }
    }

    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

} // namespace

// ---- Canvas

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

        // Kept in RGB888 once, so the bucket tool is not re-converting the
        // whole image on every click.
        m_rgb = m_source.format() == QImage::Format_RGB888
            ? m_source
            : m_source.convertToFormat(QImage::Format_RGB888);
    }

    // Replaces the mask wholesale, from a saved maskId or a legacy rect.
    void setMask(const QImage& mask)
    {
        if (mask.isNull()) {
            m_mask.fill(0);
        } else {
            m_mask = mask.convertToFormat(QImage::Format_Grayscale8);
            // Defensive: a mask of the wrong size would index out of bounds.
            if (m_mask.size() != m_source.size())
                m_mask = m_mask.scaled(m_source.size(), Qt::IgnoreAspectRatio,
                                       Qt::FastTransformation);
        }

        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
    }

    QImage mask() const { return m_mask; }

    void setTool(Tool tool)
    {
        // A tool change cancels an in-flight rect drag, or its half-painted
        // preview would linger.
        if (m_dragging) {
            m_dragging = false;
            m_currentRect = QRect();
            update();
        }
        m_tool = tool;
    }

    void setErase(bool erase) { m_erase = erase; }
    void setBrushSize(int pixels) { m_brushSize = qMax(1, pixels); }
    void setBucketTolerance(int tolerance) { m_tolerance = qMax(0, tolerance); }

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
        for (int y = 0; y < m_mask.height(); ++y) {
            uchar* row = m_mask.scanLine(y);
            for (int x = 0; x < m_mask.width(); ++x)
                row[x] = uchar(255 - row[x]);
        }

        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
    }

    void onMaskChanged(std::function<void()> callback) { m_onChanged = std::move(callback); }
    void onHistoryChanged(std::function<void()> callback)
    {
        m_onHistoryChanged = std::move(callback);
    }

    // The overlay tint is session-only and deliberately not persisted.
    QColor overlayColor() const { return m_overlayColor; }

    void setOverlayColor(const QColor& colour)
    {
        if (!colour.isValid()) return;
        m_overlayColor = QColor(colour.red(), colour.green(), colour.blue());
        rebuildOverlay(m_mask.rect());
        update();
    }

    void setOverlayAlpha(int alpha)
    {
        m_overlayAlpha = qBound(0, alpha, 255);
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
        afterHistoryStep();
    }

    void redo()
    {
        if (m_redoStack.isEmpty()) return;
        m_undoStack.push(m_mask);
        m_mask = m_redoStack.pop();
        afterHistoryStep();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0x14, 0x14, 0x14));
        if (m_source.isNull()) return;

        const QRect destination = displayRect();
        painter.drawImage(destination, m_source);

        if (m_trimMode) {
            // In trim mode the mask's box is the crop, so everything outside
            // it is dimmed rather than the mask being tinted.
            const QRect box = maskBoundingBox(m_mask);
            if (!box.isEmpty()) {
                const QRect boxOnScreen = sourceToDisplay(box);

                QPainterPath outside;
                outside.addRect(destination);
                outside.addRect(boxOnScreen);
                outside.setFillRule(Qt::OddEvenFill);
                painter.fillPath(outside, QColor(0, 0, 0, 140));

                painter.setPen(QPen(QColor(220, 220, 220, 220), 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(boxOnScreen.adjusted(0, 0, -1, -1));
            }
        } else {
            painter.drawImage(destination, m_overlay);
        }

        if (m_tool != Tool::Rect || !m_dragging || m_currentRect.isEmpty()) return;

        QPen pen(QColor(255, 255, 255, 220));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(1);
        painter.setPen(pen);

        QColor fill = m_overlayColor;
        fill.setAlpha(70);
        painter.setBrush(fill);
        painter.drawRect(sourceToDisplay(m_currentRect).adjusted(0, 0, -1, -1));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || m_source.isNull()) return;
        const QPoint position = event->position().toPoint();

        // A click in the margin around the image pans instead, so a tool
        // never fires from a coordinate clamped to the edge.
        if (!displayRect().contains(position)) {
            m_panning = true;
            m_panStart = position;
            setCursor(Qt::ClosedHandCursor);
            return;
        }

        const QPoint source = displayToSource(position);
        switch (m_tool) {
        case Tool::Rect:
            m_dragStart = source;
            m_currentRect = QRect(source, source);
            m_dragging = true;
            update();
            break;

        case Tool::Brush:
            pushUndo();
            m_lastBrushPos = source;
            paintBrushSegment(source, source, m_erase);
            break;

        case Tool::Bucket:
            pushUndo();
            QApplication::setOverrideCursor(Qt::WaitCursor);
            bucketFillAt(source);
            QApplication::restoreOverrideCursor();
            break;

        case Tool::MaskFill:
            // pushUndo is inside maskFillAt, so a click on an already-filled
            // region does not spend a history slot on nothing.
            QApplication::setOverrideCursor(Qt::WaitCursor);
            maskFillAt(source);
            QApplication::restoreOverrideCursor();
            break;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_source.isNull()) return;
        const QPoint position = event->position().toPoint();

        if (m_panning) {
            m_pan += position - m_panStart;
            m_panStart = position;
            update();
            return;
        }

        const QPoint source = displayToSource(position);
        const bool overImage = displayRect().contains(position);

        if (m_tool == Tool::Rect && m_dragging) {
            m_currentRect = QRect(m_dragStart, source).normalized();
            update();
        } else if (m_tool == Tool::Brush && (event->buttons() & Qt::LeftButton) && overImage) {
            paintBrushSegment(m_lastBrushPos, source, m_erase);
            m_lastBrushPos = source;
        }

        if (!(event->buttons() & Qt::LeftButton))
            setCursor(overImage ? Qt::ArrowCursor : Qt::OpenHandCursor);
    }

    // Cursor-anchored zoom: the source pixel under the cursor stays put, and
    // the pan shifts to compensate.
    void wheelEvent(QWheelEvent* event) override
    {
        if (m_source.isNull()) {
            event->ignore();
            return;
        }

        const QPointF cursor = event->position();
        const QPointF sourceAtCursor = displayToSourceF(cursor);
        const double previousZoom = m_zoom;

        m_zoom = qBound(kMinZoom, m_zoom * std::pow(1.15, event->angleDelta().y() / 120.0),
                        kMaxZoom);
        if (qFuzzyCompare(m_zoom, previousZoom)) {
            event->accept();
            return;
        }

        const QPointF delta = cursor - sourceToDisplayF(sourceAtCursor);
        m_pan += QPoint(int(delta.x()), int(delta.y()));

        update();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;

        if (m_panning) {
            m_panning = false;
            setCursor(displayRect().contains(event->position().toPoint()) ? Qt::ArrowCursor
                                                                          : Qt::OpenHandCursor);
            return;
        }

        if (m_tool != Tool::Rect || !m_dragging) return;

        m_dragging = false;
        const QRect box = m_currentRect.intersected(m_mask.rect());
        m_currentRect = QRect();

        // A stray click is not a rect; two pixels is the smallest meaningful
        // drag.
        if (box.width() < 2 || box.height() < 2) {
            update();
            return;
        }

        pushUndo();
        {
            QPainter painter(&m_mask);
            painter.fillRect(box, m_erase ? QColor(0, 0, 0) : QColor(255, 255, 255));
        }

        rebuildOverlay(box);
        update();
        if (m_onChanged) m_onChanged();
    }

private:
    void afterHistoryStep()
    {
        rebuildOverlay(m_mask.rect());
        update();
        if (m_onChanged) m_onChanged();
        if (m_onHistoryChanged) m_onHistoryChanged();
    }

    void paintBrushSegment(QPoint from, QPoint to, bool erase)
    {
        const int radius = m_brushSize / 2 + 1;
        const QRect box = QRect(from, to)
                              .normalized()
                              .adjusted(-radius, -radius, radius, radius)
                              .intersected(m_mask.rect());
        if (box.isEmpty()) return;

        {
            QPainter painter(&m_mask);
            // Hard edges: an anti-aliased brush would write intermediate
            // values the mask has no meaning for.
            painter.setRenderHint(QPainter::Antialiasing, false);

            QPen pen(erase ? QColor(0, 0, 0) : QColor(255, 255, 255));
            pen.setWidth(m_brushSize);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawLine(from, to);
        }

        rebuildOverlay(box);
        update(sourceToDisplay(box));
        if (m_onChanged) m_onChanged();
    }

    // Fills every pixel whose colour is within tolerance of the seed's,
    // walking outward from it, and writes that region into the mask.
    void bucketFillAt(QPoint start)
    {
        if (!m_source.rect().contains(start)) return;

        const uchar* seedPixel = m_rgb.constScanLine(start.y()) + start.x() * 3;
        const int seedR = seedPixel[0];
        const int seedG = seedPixel[1];
        const int seedB = seedPixel[2];
        const int tolerance = m_tolerance;

        const auto matches = [this, seedR, seedG, seedB, tolerance](int x, int y) {
            const uchar* pixel = m_rgb.constScanLine(y) + x * 3;
            return std::abs(int(pixel[0]) - seedR) <= tolerance
                && std::abs(int(pixel[1]) - seedG) <= tolerance
                && std::abs(int(pixel[2]) - seedB) <= tolerance;
        };

        const uchar value = m_erase ? 0 : 255;
        const auto visit = [this, value](int x, int y) { m_mask.scanLine(y)[x] = value; };

        const QRect dirty = scanlineFill(m_source.size(), start, matches, visit);
        if (dirty.isEmpty()) return;

        rebuildOverlay(dirty);
        update();
        if (m_onChanged) m_onChanged();
    }

    // Fills the connected region of the mask that shares the seed's value,
    // ignoring the image. It stops at painted mask edges or the borders, so
    // it is how an outline gets filled in.
    void maskFillAt(QPoint start)
    {
        if (!m_mask.rect().contains(start)) return;

        const uchar seed = m_mask.constScanLine(start.y())[start.x()];
        const uchar expected = m_erase ? 255 : 0;
        if (seed != expected) return; // already in the state asked for

        pushUndo();
        const uchar value = m_erase ? 0 : 255;

        const auto matches = [this, seed](int x, int y) {
            return m_mask.constScanLine(y)[x] == seed;
        };
        const auto visit = [this, value](int x, int y) { m_mask.scanLine(y)[x] = value; };

        const QRect dirty = scanlineFill(m_mask.size(), start, matches, visit);
        if (dirty.isEmpty()) return;

        rebuildOverlay(dirty);
        update();
        if (m_onChanged) m_onChanged();
    }

    // The overlay is a cached tinted mirror of the mask, blitted whole on
    // paint. Only the dirty box is regenerated, so an edit never costs a
    // walk of the entire image.
    void rebuildOverlay(const QRect& dirtyIn)
    {
        const QRect dirty = dirtyIn.intersected(m_mask.rect());
        if (dirty.isEmpty()) return;

        const QRgb tint = qRgba(m_overlayColor.red(), m_overlayColor.green(),
                                m_overlayColor.blue(), m_overlayAlpha);

        for (int y = dirty.top(); y <= dirty.bottom(); ++y) {
            auto* out = reinterpret_cast<QRgb*>(m_overlay.scanLine(y));
            const uchar* mask = m_mask.constScanLine(y);
            for (int x = dirty.left(); x <= dirty.right(); ++x)
                out[x] = mask[x] ? tint : 0;
        }
    }

    // ---- Geometry

    QRect displayRect() const
    {
        if (m_source.isNull()) return rect();

        const QSize fitted = m_source.size().scaled(size(), Qt::KeepAspectRatio);
        const QSize scaled(qMax(1, int(fitted.width() * m_zoom)),
                           qMax(1, int(fitted.height() * m_zoom)));
        const QPoint centre((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
        return QRect(centre + m_pan, scaled);
    }

    QPoint displayToSource(QPoint point) const
    {
        const QRect destination = displayRect();
        if (destination.isEmpty()) return {};

        const double scaleX = double(m_source.width()) / destination.width();
        const double scaleY = double(m_source.height()) / destination.height();

        return {qBound(0, int((point.x() - destination.left()) * scaleX), m_source.width() - 1),
                qBound(0, int((point.y() - destination.top()) * scaleY), m_source.height() - 1)};
    }

    // The float versions matter for zoom anchoring: rounding here would drift
    // visibly over a long run of wheel ticks.
    QPointF displayToSourceF(QPointF point) const
    {
        const QRect destination = displayRect();
        if (destination.isEmpty()) return {};

        return {(point.x() - destination.left()) * double(m_source.width()) / destination.width(),
                (point.y() - destination.top()) * double(m_source.height())
                    / destination.height()};
    }

    QPointF sourceToDisplayF(QPointF point) const
    {
        const QRect destination = displayRect();
        if (destination.isEmpty() || m_source.isNull()) return {};

        return {destination.left() + point.x() * destination.width() / m_source.width(),
                destination.top() + point.y() * destination.height() / m_source.height()};
    }

    QRect sourceToDisplay(QRect box) const
    {
        const QRect destination = displayRect();
        if (destination.isEmpty() || m_source.isNull()) return {};

        const double scaleX = double(destination.width()) / m_source.width();
        const double scaleY = double(destination.height()) / m_source.height();

        return {destination.left() + int(box.left() * scaleX),
                destination.top() + int(box.top() * scaleY),
                qMax(1, int(box.width() * scaleX)), qMax(1, int(box.height() * scaleY))};
    }

    void pushUndo()
    {
        m_undoStack.push(m_mask);
        while (m_undoStack.size() > kMaxHistory)
            m_undoStack.removeFirst();
        m_redoStack.clear();
        if (m_onHistoryChanged) m_onHistoryChanged();
    }

    static constexpr int kMaxHistory = 32;
    static constexpr double kMinZoom = 0.5;
    static constexpr double kMaxZoom = 64.0;

    QImage m_source;
    QImage m_rgb;     // the source as RGB888, for the bucket's colour test
    QImage m_mask;    // Grayscale8, source-sized: 0 is clear, 255 is masked
    QImage m_overlay; // ARGB32, the tinted mirror of the mask

    Tool m_tool = Tool::Brush;
    bool m_erase = false;
    int m_brushSize = 30;
    int m_tolerance = 16;
    bool m_trimMode = false;

    QColor m_overlayColor{102, 170, 102}; // the app's accent green
    int m_overlayAlpha = 110;

    QPoint m_dragStart;
    QRect m_currentRect;
    bool m_dragging = false;
    QPoint m_lastBrushPos;

    bool m_panning = false;
    QPoint m_panStart;

    // The pan offsets the display from its centred fit position; the wheel
    // zoom and a margin drag both move it.
    double m_zoom = 1.0;
    QPoint m_pan;

    QStack<QImage> m_undoStack;
    QStack<QImage> m_redoStack;

    std::function<void()> m_onChanged;
    std::function<void()> m_onHistoryChanged;
};

// ---- Dialog

ClipEditorDialog::ClipEditorDialog(const QImage& source, const ImageEdits& initial,
                                   WorkflowInputCache* cache, QWidget* parent)
    : ChromedDialog(parent), m_cache(cache)
{
    setWindowTitle(u"Clip Editor"_s);
    setMinimumSize(920, 920);
    resize(1024, 920);

    // The tool buttons override the dialog-wide red checked state, because a
    // tool is a selector rather than a destructive toggle. Erase keeps red.
    setStyleSheet(
        u"QSlider::groove:horizontal { background: #1a1a1a; height: 4px; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #336633; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #66aa66; border: 1px solid #336633;"
        " width: 12px; margin: -5px 0; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { background: #77bb77; }"
        "QPushButton#ClipEditorToolBtn:checked { background: #182418; color: #5a9a5a;"
        " border-color: #2a4a2a; }"
        "QPushButton#ClipEditorToolBtn:checked:hover { background: #1e2e1e;"
        " color: #7acc7a; border-color: #3a6a3a; }"_s);

    m_canvas = new ClipCanvas(source, this);

    // A saved mask wins; a legacy edit with only a rect is seeded from it.
    if (m_cache && !initial.maskId.isEmpty()) {
        m_canvas->setMask(m_cache->loadMask(initial.maskId));
    } else if (initial.enabled && !initial.cropRect.isEmpty()) {
        QImage seed(source.size(), QImage::Format_Grayscale8);
        seed.fill(0);
        {
            QPainter painter(&seed);
            painter.fillRect(initial.cropRect.intersected(seed.rect()), QColor(255, 255, 255));
        }
        m_canvas->setMask(seed);
    }

    m_canvas->setTrimMode(initial.trimToCrop);
    m_canvas->onMaskChanged([this]() { updateRectLabel(); });

    // ---- Tools
    m_toolGroup = new QButtonGroup(this);
    auto makeToolButton = [this](const QString& text, Tool tool, bool checked = false) {
        auto* button = new QPushButton(text, this);
        button->setObjectName(u"ClipEditorToolBtn"_s);
        button->setCheckable(true);
        button->setChecked(checked);
        button->setCursor(Qt::PointingHandCursor);
        m_toolGroup->addButton(button, int(tool));

        connect(button, &QPushButton::clicked, this, [this, tool]() {
            m_canvas->setTool(tool);
            updateToolControls();
        });
        return button;
    };

    QPushButton* rectBtn = makeToolButton(u"Rect"_s, Tool::Rect);
    QPushButton* brushBtn = makeToolButton(u"Brush"_s, Tool::Brush, true);
    QPushButton* bucketBtn = makeToolButton(u"Bucket"_s, Tool::Bucket);
    QPushButton* maskFillBtn = makeToolButton(u"Mask Fill"_s, Tool::MaskFill);

    bucketBtn->setToolTip(u"Flood fill source-similar pixels into the mask."_s);
    maskFillBtn->setToolTip(u"Flood fill the empty mask region until the painted mask or the "
                            "image edge contains it. With Erase, clears a contained region."_s);
    m_canvas->setTool(Tool::Brush);

    // ---- History
    auto* undoBtn = new QPushButton(u"Undo"_s, this);
    auto* redoBtn = new QPushButton(u"Redo"_s, this);
    undoBtn->setEnabled(false);
    redoBtn->setEnabled(false);
    undoBtn->setToolTip(u"Undo (Ctrl+Z)"_s);
    redoBtn->setToolTip(u"Redo (Ctrl+Y)"_s);

    connect(undoBtn, &QPushButton::clicked, this, [this]() { m_canvas->undo(); });
    connect(redoBtn, &QPushButton::clicked, this, [this]() { m_canvas->redo(); });
    m_canvas->onHistoryChanged([this, undoBtn, redoBtn]() {
        undoBtn->setEnabled(m_canvas->canUndo());
        redoBtn->setEnabled(m_canvas->canRedo());
    });

    connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, this,
            [this]() { m_canvas->undo(); });
    connect(new QShortcut(QKeySequence::Redo, this), &QShortcut::activated, this,
            [this]() { m_canvas->redo(); });

    // ---- Erase modifier
    m_eraseBtn = new QPushButton(u"Erase"_s, this);
    m_eraseBtn->setObjectName(u"ClipEditorEraseBtn"_s);
    m_eraseBtn->setCheckable(true);
    m_eraseBtn->setCursor(Qt::PointingHandCursor);
    m_eraseBtn->setToolTip(
        u"When on, the active tool removes from the mask instead of adding to it."_s);
    connect(m_eraseBtn, &QPushButton::toggled, this,
            [this](bool on) { m_canvas->setErase(on); });

    // ---- Sliders
    // The labels are fixed width, or the row reshuffles mid-drag as the
    // number changes length.
    m_brushLabel = new QLabel(u"Size 30"_s, this);
    m_brushLabel->setFixedWidth(
        m_brushLabel->fontMetrics().horizontalAdvance(u"Size 200"_s) + 24);

    m_brushSize = new QSlider(Qt::Horizontal, this);
    m_brushSize->setRange(1, 200);
    m_brushSize->setValue(30);
    connect(m_brushSize, &QSlider::valueChanged, this, [this](int value) {
        m_canvas->setBrushSize(value);
        m_brushLabel->setText(u"Size %1"_s.arg(value));
    });

    m_toleranceLabel = new QLabel(u"Tolerance 16"_s, this);
    m_toleranceLabel->setFixedWidth(
        m_toleranceLabel->fontMetrics().horizontalAdvance(u"Tolerance 255"_s) + 48);

    m_tolerance = new QSlider(Qt::Horizontal, this);
    m_tolerance->setRange(0, 255);
    m_tolerance->setValue(16);
    connect(m_tolerance, &QSlider::valueChanged, this, [this](int value) {
        m_canvas->setBucketTolerance(value);
        m_toleranceLabel->setText(u"Tolerance %1"_s.arg(value));
    });

    // ---- Overlay tint
    auto* colourBtn = new QPushButton(this);
    colourBtn->setFixedSize(24, 24);
    colourBtn->setCursor(Qt::PointingHandCursor);
    colourBtn->setToolTip(u"Overlay color"_s);

    auto applySwatch = [this, colourBtn]() {
        colourBtn->setStyleSheet(
            u"background: %1; border: 1px solid #2a2a2a; border-radius: 3px;"_s.arg(
                m_canvas->overlayColor().name()));
    };
    applySwatch();

    connect(colourBtn, &QPushButton::clicked, this, [this, applySwatch]() {
        // Wrapped so the picker gets the app's own titlebar.
        ChromedDialog wrapper(this);
        wrapper.setWindowTitle(u"Overlay color"_s);

        auto* picker = new QColorDialog(m_canvas->overlayColor(), wrapper.contentArea());
        picker->setOptions(QColorDialog::DontUseNativeDialog | QColorDialog::NoButtons);
        picker->setWindowFlags(Qt::Widget);
        picker->setSizeGripEnabled(false);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             wrapper.contentArea());
        connect(buttons, &QDialogButtonBox::accepted, &wrapper, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &wrapper, &QDialog::reject);
        connect(picker, &QColorDialog::colorSelected, &wrapper,
                [&wrapper](const QColor&) { wrapper.accept(); });
        // Escape on the embedded picker only hides the picker, so it is
        // forwarded to close the wrapper too.
        connect(picker, &QDialog::rejected, &wrapper, &QDialog::reject);

        auto* layout = new QVBoxLayout(wrapper.contentArea());
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(picker, 1);
        layout->addWidget(buttons);

        if (wrapper.exec() != QDialog::Accepted) return;
        const QColor chosen = picker->currentColor();
        if (!chosen.isValid()) return;

        m_canvas->setOverlayColor(chosen);
        applySwatch();
    });

    auto* opacityLabel = new QLabel(u"Opacity 110"_s, this);
    opacityLabel->setFixedWidth(
        opacityLabel->fontMetrics().horizontalAdvance(u"Opacity 255"_s) + 48);

    auto* opacitySlider = new QSlider(Qt::Horizontal, this);
    opacitySlider->setRange(0, 255);
    opacitySlider->setValue(110);
    connect(opacitySlider, &QSlider::valueChanged, this, [this, opacityLabel](int value) {
        m_canvas->setOverlayAlpha(value);
        opacityLabel->setText(u"Opacity %1"_s.arg(value));
    });

    // ---- Bottom controls
    m_rectLabel = new QLabel(this);
    m_rectLabel->setObjectName(u"ClipEditorRectLabel"_s);
    m_rectLabel->setWordWrap(true);

    m_trimToCrop = new QCheckBox(u"Crop only (no mask)"_s, this);
    m_trimToCrop->setChecked(initial.trimToCrop);
    m_trimToCrop->setToolTip(
        u"Off: the painted mask defines MASK. The output is source-sized, and LoadImage's "
        "IMAGE is the original picture with MASK set inside the painted region.\n"
        "On: a rect-only crop. The output is that rect cropped from the source, with alpha "
        "255 everywhere and no mask."_s);
    connect(m_trimToCrop, &QCheckBox::toggled, this, [this](bool on) {
        // Trim mode is only a crop, so keeping mask data would be misleading.
        if (on) m_canvas->clearMask();
        applyTrimModeUI(on);
    });

    auto* clearBtn = new QPushButton(u"Clear mask"_s, this);
    connect(clearBtn, &QPushButton::clicked, this, [this]() { m_canvas->clearMask(); });

    m_invertBtn = new QPushButton(u"Invert mask"_s, this);
    m_invertBtn->setToolTip(
        u"Flip every mask pixel. Useful when the keep region is easier to paint than the "
        "masked one."_s);
    connect(m_invertBtn, &QPushButton::clicked, this, [this]() { m_canvas->invertMask(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(u"Apply"_s);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ---- Layout
    auto* sidebar = new QWidget(this);
    sidebar->setFixedWidth(220);

    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(6);

    // Two by two keeps the four tools compact.
    auto* toolGrid = new QGridLayout;
    toolGrid->setSpacing(6);
    toolGrid->addWidget(rectBtn, 0, 0);
    toolGrid->addWidget(brushBtn, 0, 1);
    toolGrid->addWidget(bucketBtn, 1, 0);
    toolGrid->addWidget(maskFillBtn, 1, 1);
    sidebarLayout->addLayout(toolGrid);

    sidebarLayout->addWidget(m_eraseBtn);

    auto* historyRow = new QHBoxLayout;
    historyRow->setSpacing(6);
    historyRow->addWidget(undoBtn);
    historyRow->addWidget(redoBtn);
    sidebarLayout->addLayout(historyRow);

    sidebarLayout->addSpacing(8);
    sidebarLayout->addWidget(m_brushLabel);
    sidebarLayout->addWidget(m_brushSize);

    sidebarLayout->addSpacing(4);
    sidebarLayout->addWidget(m_toleranceLabel);
    sidebarLayout->addWidget(m_tolerance);

    sidebarLayout->addSpacing(4);
    auto* colourRow = new QHBoxLayout;
    colourRow->setSpacing(8);
    colourRow->addWidget(colourBtn);
    colourRow->addWidget(opacityLabel);
    colourRow->addStretch();
    sidebarLayout->addLayout(colourRow);
    sidebarLayout->addWidget(opacitySlider);

    sidebarLayout->addSpacing(12);
    sidebarLayout->addWidget(clearBtn);
    sidebarLayout->addWidget(m_invertBtn);
    sidebarLayout->addWidget(m_trimToCrop);
    sidebarLayout->addStretch();

    auto* split = new QHBoxLayout;
    split->setSpacing(8);
    split->addWidget(sidebar);
    split->addWidget(m_canvas, 1);

    // The read-out shares the bottom row with the dialog buttons, so it stays
    // visible without taking sidebar space.
    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(8);
    statusRow->addWidget(m_rectLabel, 1);
    statusRow->addWidget(buttons);

    auto* contentLayout = new QVBoxLayout(contentArea());
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(8);
    contentLayout->addLayout(split, 1);
    contentLayout->addLayout(statusRow);

    // Applied without clearing: legacy data carrying both a mask and
    // trimToCrop should load intact.
    applyTrimModeUI(initial.trimToCrop);
}

ImageEdits ClipEditorDialog::result() const
{
    return m_result;
}

void ClipEditorDialog::accept()
{
    // An empty mask means no edits at all, which the caller reads as "drop
    // the maskId and clear the edits".
    const QImage finalMask = m_canvas->mask();
    const QRect box = maskBoundingBox(finalMask);

    if (box.isEmpty()) {
        m_result = ImageEdits{};
        QDialog::accept();
        return;
    }

    m_result.enabled = true;
    m_result.cropRect = box;
    m_result.trimToCrop = m_trimToCrop->isChecked();

    // Trim mode discards the mask at render time, so not saving one avoids a
    // stray cache file and keeps the card's label honest.
    m_result.maskId =
        (!m_result.trimToCrop && m_cache) ? m_cache->saveMask(finalMask) : QString();

    QDialog::accept();
}

void ClipEditorDialog::updateRectLabel()
{
    const QRect box = maskBoundingBox(m_canvas->mask());
    if (box.isEmpty()) {
        m_rectLabel->setText(u"No mask painted - output will be the source unchanged."_s);
        return;
    }

    m_rectLabel->setText((m_trimToCrop->isChecked() ? u"Crop bbox: x=%1 y=%2  %3 x %4"_s
                                                    : u"Mask bbox: x=%1 y=%2  %3 x %4"_s)
                             .arg(box.x())
                             .arg(box.y())
                             .arg(box.width())
                             .arg(box.height()));
}

void ClipEditorDialog::updateToolControls()
{
    // Greying out what the active tool does not use makes its parameters
    // obvious without a label.
    const int id = m_toolGroup->checkedId();
    const bool isBrush = id == int(Tool::Brush);
    const bool isBucket = id == int(Tool::Bucket);

    m_brushSize->setEnabled(isBrush);
    m_brushLabel->setEnabled(isBrush);
    m_tolerance->setEnabled(isBucket);
    m_toleranceLabel->setEnabled(isBucket);
}

void ClipEditorDialog::applyTrimModeUI(bool on)
{
    m_canvas->setTrimMode(on);

    if (on) {
        // The painting tools would silently do nothing in trim mode, so Rect
        // is forced and the rest locked out.
        if (QAbstractButton* rectBtn = m_toolGroup->button(int(Tool::Rect)))
            rectBtn->setChecked(true);
        m_canvas->setTool(Tool::Rect);
        m_eraseBtn->setChecked(false);
        m_canvas->setErase(false);
    }

    for (Tool tool : {Tool::Brush, Tool::Bucket, Tool::MaskFill})
        if (QAbstractButton* button = m_toolGroup->button(int(tool))) button->setEnabled(!on);

    m_eraseBtn->setEnabled(!on);
    m_invertBtn->setEnabled(!on);

    updateToolControls();
    updateRectLabel();
}

} // namespace tc
