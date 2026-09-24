#include "brushtool.h"

#include <QPainter>

// 面片由几何层解析，这里只补上笔触所需的归一化 UV。
bool BrushTool::resolveTarget(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                              const QPointF &point, PerspectiveQuad *quad, QPointF *uv)
{
    PerspectiveQuad target;
    if (!resolveQuad(planes, canvasSize, point, &target))
        return false;
    bool valid = false;
    const QPointF position = target.mapCanvasToUv(point, &valid);
    if (!valid)
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
    m_drawing = true;
    return m_engine.beginStroke(layer, m_quad, uv);
}

// 锚定后笔触可以越过面片边界继续延伸，只要求点仍在该面片的单应有效范围内。
QRect BrushTool::move(QImage &layer, const QPointF &point)
{
    if (!m_drawing)
        return {};
    bool valid = false;
    const QPointF uv = m_quad.mapCanvasToUv(point, &valid);
    if (!valid)
        return {};
    return m_engine.drawStrokeTo(layer, m_quad, uv);
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
    m_engine.applyDab(painter, quad, uv);
}
