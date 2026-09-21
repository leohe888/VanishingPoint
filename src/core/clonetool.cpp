#include "clonetool.h"

#include <QPainter>

QPointF CloneTool::originalMarker() const
{
    QPointF marker;
    if (m_hasSource && m_sourceMapping.mapForward(m_source, &marker))
        return marker;
    return m_marker;
}

void CloneTool::setAligned(bool value)
{
    if (m_aligned == value)
        return;
    m_aligned = value;
    // 未落笔时立即换语义：偏移作废，源点先回到原位，等下次落笔再重新锚定
    if (!m_drawing) {
        m_hasOffset = false;
        m_marker = originalMarker();
    }
}

bool CloneTool::pickSource(const QVector<PerspectivePlane> &planes, const QSize &canvasSize, const QPointF &point)
{
    PerspectiveFacet facet;
    if (!resolveFacet(planes, canvasSize, point, &facet))
        return false;
    m_sourceMapping = facet.surfaceToCanvasTransform();
    QPointF surface;
    if (!m_sourceMapping.mapInverse(point, &surface))
        return false;
    m_source = surface;
    m_marker = point;
    m_hasSource = true;
    m_hasOffset = false; // 换了源点，落点偏移需要重新锚定
    return true;
}

PerspectiveTransform CloneTool::targetAt(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                                        const QPointF &point) const
{
    if (m_drawing)
        return m_targetMapping;
    PerspectiveFacet facet;
    if (!resolveFacet(planes, canvasSize, point, &facet))
        return {};
    return facet.surfaceToCanvasTransform();
}

// 对齐模式下偏移一经锁定就跨笔保留，源点于是跟着光标走
QPointF CloneTool::anchoredOffset(const QPointF &position) const
{
    if (m_aligned && m_hasOffset)
        return m_offset;
    return m_source - position;
}

QRect CloneTool::begin(QImage &layer, const QImage &source, const QVector<PerspectivePlane> &planes,
                       const QSize &canvasSize, const QPointF &point)
{
    if (!m_hasSource || layer.isNull())
        return {};
    m_targetMapping = targetAt(planes, canvasSize, point);
    QPointF position;
    if (!m_targetMapping.mapInverse(point, &position))
        return {};
    m_offset = anchoredOffset(position);
    m_hasOffset = m_drawing = true;
    return m_engine.beginStroke(layer, source, m_targetMapping.forward(), m_sourceMapping.forward(),
                                m_offset, position);
}

QRect CloneTool::move(QImage &layer, const QPointF &point)
{
    QPointF position;
    if (!m_drawing || !m_targetMapping.mapInverse(point, &position))
        return {};
    return m_engine.drawStrokeTo(layer, position);
}

void CloneTool::end()
{
    m_engine.endStroke();
    m_drawing = false;
    if (!m_aligned) {
        m_hasOffset = false;
        m_marker = originalMarker();
    }
}

void CloneTool::hover(const QVector<PerspectivePlane> &planes, const QSize &canvasSize, const QPointF &point)
{
    if (!m_hasSource)
        return;
    m_marker = originalMarker();
    // 非对齐模式尚未落笔：源点停在原位，不做偏移推算
    if (!m_hasOffset)
        return;
    QPointF position;
    if (!targetAt(planes, canvasSize, point).mapInverse(point, &position))
        return;
    QPointF marker;
    if (m_sourceMapping.mapForward(position + m_offset, &marker))
        m_marker = marker;
}

void CloneTool::renderPreview(QPainter &painter, const QImage &source, const QVector<PerspectivePlane> &planes,
                              const QSize &canvasSize, const QPointF &point)
{
    if (!m_hasSource)
        return;
    const PerspectiveTransform targetMapping = targetAt(planes, canvasSize, point);
    QPointF position;
    if (!targetMapping.mapInverse(point, &position))
        return;
    // 落笔期间沿用锁定的偏移——非对齐模式这一笔的源点也在跟着光标走；
    // 抬笔后按当前落点算下一笔的偏移。两种情况取样位置都正好落在源点十字上。
    const QPointF offset = m_drawing ? m_offset : anchoredOffset(position);
    m_engine.setPreview(source, targetMapping.forward(), m_sourceMapping.forward(), offset);
    const QRect area = m_engine.dabRect(position);
    if (area.isEmpty())
        return;
    QImage dab(area.size(), QImage::Format_ARGB32_Premultiplied);
    dab.fill(Qt::transparent);
    m_engine.renderDab(dab, area, position);
    painter.drawImage(area.topLeft(), dab);
}
