#include "brushtool.h"

#include <QLineF>
#include <QPainter>
#include <QRadialGradient>
#include <QTransform>
#include <QtMath>
#include <cmath>

// 与图章和浮动选区共用展开平面到画布的映射。
bool BrushTool::resolveTarget(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                              const QPointF &point, PerspectiveTransform *mapping, QPointF *position)
{
    PerspectiveQuad target;
    if (!resolveQuad(planes, canvasSize, point, &target))
        return false;
    const PerspectiveTransform projection = target.surfaceToCanvasTransform();
    if (!projection.mapInverse(point, position))
        return false;
    *mapping = projection;
    return true;
}

QRect BrushTool::begin(QImage &layer, const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                       const QPointF &point, const std::function<void(const QRect &)> &prepare,
                       const QImage &healReference)
{
    PerspectiveTransform mapping;
    QPointF position;
    if (layer.isNull() || !resolveTarget(planes, canvasSize, point, &mapping, &position))
        return {};
    m_mapping = mapping;
    m_drawing = true;
    m_lastPosition = position;
    m_healReference = healReference;
    m_healDabs.clear();
    m_healDirty = {};
    m_beforeHealStroke = m_healMode != HealingBlend::Off && !healReference.isNull() ? layer : QImage();
    QPainter painter(&layer);
    if (!m_selectionClip.isEmpty()) painter.setClipPath(m_selectionClip);
    const QRect dirty = applyDab(painter, m_mapping, position, prepare);
    if (!m_beforeHealStroke.isNull() && !dirty.isEmpty()) {
        m_healDabs.append({position, m_diameter, m_hardness, m_opacity, m_brushColor});
        m_healDirty = dirty;
    }
    return dirty;
}

QRect BrushTool::end(QImage *layer)
{
    QRect dirty;
    if (layer && m_drawing && !m_beforeHealStroke.isNull() && !m_healDabs.isEmpty()) {
        dirty = m_healDirty.intersected(layer->rect());
        QPainter painter(layer);
        if (!m_selectionClip.isEmpty()) painter.setClipPath(m_selectionClip);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(dirty, m_beforeHealStroke, dirty);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        // Rebuild only this stroke from its original coverage, avoiding a second
        // opacity application over the unhealed stroke or changes to older paint.
        const Dab settings{{}, m_diameter, m_hardness, m_opacity, m_brushColor};
        m_finalizingHeal = true;
        for (const Dab &dab : m_healDabs) {
            m_diameter = dab.diameter; m_hardness = dab.hardness;
            m_opacity = dab.opacity; m_brushColor = dab.color;
            applyDab(painter, m_mapping, dab.position, {}, m_healReference);
        }
        m_finalizingHeal = false;
        m_diameter = settings.diameter; m_hardness = settings.hardness;
        m_opacity = settings.opacity; m_brushColor = settings.color;
    }
    m_drawing = false;
    m_healReference = QImage();
    m_beforeHealStroke = QImage();
    m_healDabs.clear();
    m_healDirty = {};
    return dirty;
}

// 锚定后笔触可以越过面片边界继续延伸，只要求点仍在该面片的单应有效范围内。
QRect BrushTool::move(QImage &layer, const QPointF &point, const std::function<void(const QRect &)> &prepare)
{
    if (!m_drawing)
        return {};
    QPointF position;
    if (!m_mapping.mapInverse(point, &position))
        return {};
    return drawStrokeTo(layer, position, prepare);
}

// 悬停和拖动预览均显示普通笔触，修复只发生在结束整笔之后。
void BrushTool::renderPreview(QPainter &painter, const QVector<PerspectivePlane> &planes,
                              const QSize &canvasSize, const QPointF &point) const
{
    PerspectiveTransform mapping = m_mapping;
    QPointF position;
    if (m_drawing) {
        if (!mapping.mapInverse(point, &position))
            return;
    } else if (!resolveTarget(planes, canvasSize, point, &mapping, &position)) {
        return;
    }
    // 光标预览的 painter.device() 只是局部的小纹理，不能用其尺寸裁剪画布坐标。
    paintDab(painter, mapping.forward(), position);
}

QRectF BrushTool::previewBounds(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                               const QPointF &point) const
{
    PerspectiveTransform mapping = m_mapping;
    QPointF position;
    if (m_drawing) {
        if (!mapping.mapInverse(point, &position)) return {};
    } else if (!resolveTarget(planes, canvasSize, point, &mapping, &position)) return {};
    const qreal radius = m_diameter / 2.0;
    const QRectF bounds(position - QPointF(radius, radius), QSizeF(m_diameter, m_diameter));
    return mapping.canProject(bounds) ? mapping.forward().mapRect(bounds) : QRectF();
}

QRect BrushTool::applyDab(QPainter &painter, const PerspectiveTransform &mapping, const QPointF &position,
                         const std::function<void(const QRect &)> &prepare, const QImage &healReference) const
{
    const qreal radius = m_diameter / 2.0;

    const QTransform &surfaceToCanvas = mapping.forward();
    const QRectF bounds(position.x() - radius, position.y() - radius, m_diameter, m_diameter);
    if (!mapping.canProject(bounds))
        return {};
    QRect dirty = surfaceToCanvas.mapRect(bounds)
        .intersected(QRectF(0, 0, painter.device()->width(), painter.device()->height()))
        .toAlignedRect().adjusted(-1, -1, 1, 1);
    if (!m_selectionClip.isEmpty()) dirty = dirty.intersected(m_selectionClip.boundingRect().toAlignedRect());
    if (dirty.isEmpty()) return {};
    if (prepare) prepare(dirty);

    if (m_finalizingHeal && !healReference.isNull() && !dirty.isEmpty()) {
        QImage dab(dirty.size(), QImage::Format_ARGB32_Premultiplied);
        if (dab.isNull()) {
            paintDab(painter, surfaceToCanvas, position);
            return dirty;
        }
        dab.fill(Qt::transparent);
        // Render the ordinary brush coverage first, then adjust only its colors.
        QPainter dabPainter(&dab);
        dabPainter.translate(-dirty.x(), -dirty.y());
        paintDab(dabPainter, surfaceToCanvas, position);
        dabPainter.end();
        HealingBlend heal(m_healMode, healReference, mapping, position, radius, m_brushColor);
        for (int y = 0; y < dab.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(dab.scanLine(y));
            for (int x = 0; x < dab.width(); ++x) {
                QPointF surface;
                if (qAlpha(row[x]) && mapping.mapInverse(QPointF(dirty.x() + x + .5, dirty.y() + y + .5), &surface))
                    row[x] = heal.blend(row[x], surface);
            }
        }
        painter.drawImage(dirty.topLeft(), dab);
        return dirty;
    }

    paintDab(painter, surfaceToCanvas, position);
    return dirty;
}

void BrushTool::paintDab(QPainter &painter, const QTransform &surfaceToCanvas, const QPointF &position) const
{
    const qreal radius = m_diameter / 2.0;
    const qreal hardness = m_hardness / 100.0;
    QRadialGradient gradient(position, radius);
    QColor core = m_brushColor;
    core.setAlphaF(m_opacity / 100.0);
    gradient.setColorAt(0.0, core);
    if (hardness < 1.0) {
        gradient.setColorAt(hardness, core);
        QColor edge = m_brushColor;
        edge.setAlphaF(0.0);
        gradient.setColorAt(1.0, edge);
    } else {
        gradient.setColorAt(1.0, core);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setWorldTransform(surfaceToCanvas, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(position, radius, radius);
    painter.restore();

}

QRect BrushTool::drawStrokeTo(QImage &layer, const QPointF &position, const std::function<void(const QRect &)> &prepare)
{
    const qreal step = (m_diameter / 2.0) * 0.35;
    const qreal distance = QLineF(m_lastPosition, position).length();
    if (distance < 1e-12)
        return {};
    const qreal requested = std::ceil(distance / step);
    if (!qIsFinite(requested) || requested > 2048
        || !m_mapping.canProject(QRectF(m_lastPosition, position).normalized()))
        return {};
    const int count = qMax(1, int(requested));

    QPainter painter(&layer);
    if (!m_selectionClip.isEmpty()) painter.setClipPath(m_selectionClip);
    QRect dirty;
    for (int i = 1; i <= count; ++i) {
        const QPointF dabPosition = m_lastPosition + (position - m_lastPosition) * (qreal(i) / qreal(count));
        const QRect r = applyDab(painter, m_mapping, dabPosition, prepare);
        if (!m_beforeHealStroke.isNull() && !r.isEmpty()) {
            m_healDabs.append({dabPosition, m_diameter, m_hardness, m_opacity, m_brushColor});
            m_healDirty = m_healDirty.united(r);
        }
        dirty = dirty.isEmpty() ? r : dirty.united(r);
    }
    m_lastPosition = position;
    return dirty;
}
