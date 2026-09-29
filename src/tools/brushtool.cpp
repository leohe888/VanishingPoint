#include "brushtool.h"

#include <QLineF>
#include <QPainter>
#include <QRadialGradient>
#include <QTransform>
#include <QtMath>
#include <cmath>

namespace {

qreal uvRadius(const PerspectiveQuad &quad, qreal diameter)
{
    const qreal planeWidth = (QLineF(quad.canvasCorners()[0], quad.canvasCorners()[1]).length() +
                              QLineF(quad.canvasCorners()[3], quad.canvasCorners()[2]).length()) / 2.0;
    return (diameter / 2.0) / qMax(40.0, planeWidth);
}

} // namespace

// 面片由几何层解析，这里只补上笔触所需的归一化 UV。
bool BrushTool::resolveTarget(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                              const QPointF &point, PerspectiveQuad *quad, QPointF *uv)
{
    PerspectiveQuad target;
    if (!resolveQuad(planes, canvasSize, point, &target))
        return false;
    QPointF position;
    if (!target.uvToCanvasTransform().mapInverse(point, &position))
        return false;
    *quad = target;
    *uv = position;
    return true;
}

QRect BrushTool::begin(QImage &layer, const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                       const QPointF &point)
{
    PerspectiveQuad quad;
    QPointF uv;
    if (layer.isNull() || !resolveTarget(planes, canvasSize, point, &quad, &uv))
        return {};
    m_quad = quad;
    m_mapping = quad.uvToCanvasTransform();
    m_drawing = true;
    m_lastUv = uv;
    QPainter painter(&layer);
    return applyDab(painter, m_quad, uv);
}

// 锚定后笔触可以越过面片边界继续延伸，只要求点仍在该面片的单应有效范围内。
QRect BrushTool::move(QImage &layer, const QPointF &point)
{
    if (!m_drawing)
        return {};
    QPointF uv;
    if (!m_mapping.mapInverse(point, &uv))
        return {};
    return drawStrokeTo(layer, uv);
}

// 所见即所得：把即将落下的那一个笔触点直接画到光标处。
// 与真实落笔走同一条 applyDab，因此颜色、软边、不透明度、透视都与落笔结果一致。
void BrushTool::renderPreview(QPainter &painter, const QVector<PerspectivePlane> &planes,
                              const QSize &canvasSize, const QPointF &point) const
{
    PerspectiveQuad quad;
    QPointF uv;
    if (!resolveTarget(planes, canvasSize, point, &quad, &uv))
        return;
    applyDab(painter, quad, uv);
}

QRect BrushTool::applyDab(QPainter &painter, const PerspectiveQuad &quad, const QPointF &uv) const
{
    const qreal radiusUv = uvRadius(quad, m_diameter);
    const qreal hardness = m_hardness / 100.0;

    const PerspectiveTransform mapping = (&quad == &m_quad) ? m_mapping : quad.uvToCanvasTransform();
    const QTransform &uvToCanvas = mapping.forward();
    const QRectF bounds(uv.x() - radiusUv, uv.y() - radiusUv, radiusUv * 2, radiusUv * 2);
    if (!mapping.isValid() || !PerspectiveTransform::mapsDomain(uvToCanvas, bounds))
        return {};

    QRadialGradient gradient(uv, radiusUv);
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
    painter.setWorldTransform(uvToCanvas, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(QPointF(uv), radiusUv, radiusUv);
    painter.restore();

    return uvToCanvas.mapRect(bounds).intersected(QRectF(0, 0, painter.device()->width(), painter.device()->height()))
        .toAlignedRect().adjusted(-1, -1, 1, 1);
}

QRect BrushTool::drawStrokeTo(QImage &layer, const QPointF &uv)
{
    const qreal radiusUv = uvRadius(m_quad, m_diameter);
    const qreal step = qMax(0.001, radiusUv * 0.35);
    const qreal distance = QLineF(m_lastUv, uv).length();
    if (distance < 1e-12)
        return {};
    const qreal requested = std::ceil(distance / step);
    if (!qIsFinite(requested) || requested > 2048
        || !PerspectiveTransform::mapsDomain(m_mapping.forward(), QRectF(m_lastUv, uv).normalized()))
        return {};
    const int count = qMax(1, int(requested));

    QPainter painter(&layer);
    QRect dirty;
    for (int i = 1; i <= count; ++i) {
        const QPointF uvi = m_lastUv + (uv - m_lastUv) * (qreal(i) / qreal(count));
        const QRect r = applyDab(painter, m_quad, uvi);
        dirty = dirty.isEmpty() ? r : dirty.united(r);
    }
    m_lastUv = uv;
    return dirty;
}
