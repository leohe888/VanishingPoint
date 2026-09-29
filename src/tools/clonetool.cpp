#include "clonetool.h"

#include <QLineF>
#include <QPainter>
#include <QtMath>

#include <cmath>

namespace {

// 源图使用预乘 alpha，双线性取样时透明边缘不会带入黑色。
QRgb sample(const QImage &image, const QPointF &point, qreal coverage)
{
    const qreal x = point.x() - .5, y = point.y() - .5;
    const int x0 = qFloor(x), y0 = qFloor(y);
    const qreal fx = x - x0, fy = y - y0;
    qreal r = 0, g = 0, b = 0, a = 0;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            if (!image.rect().contains(x0 + i, y0 + j))
                continue;
            const QRgb pixel = image.pixel(x0 + i, y0 + j);
            const qreal weight = (i ? fx : 1 - fx) * (j ? fy : 1 - fy) * coverage;
            r += qRed(pixel) * weight;
            g += qGreen(pixel) * weight;
            b += qBlue(pixel) * weight;
            a += qAlpha(pixel) * weight;
        }
    }
    return qRgba(qRound(r), qRound(g), qRound(b), qRound(a));
}

} // namespace

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
    PerspectiveQuad quad;
    if (!resolveQuad(planes, canvasSize, point, &quad))
        return false;
    m_sourceMapping = quad.surfaceToCanvasTransform();
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
    PerspectiveQuad quad;
    if (!resolveQuad(planes, canvasSize, point, &quad))
        return {};
    return quad.surfaceToCanvasTransform();
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
    m_stamp = makeStampContext(source, m_targetMapping.forward(), m_sourceMapping.forward(), m_offset);
    m_lastPosition = position;
    return applyDab(layer, position);
}

QRect CloneTool::move(QImage &layer, const QPointF &point)
{
    QPointF position;
    if (!m_drawing || !m_targetMapping.mapInverse(point, &position))
        return {};
    const qreal distance = QLineF(m_lastPosition, position).length();
    if (!std::isfinite(distance) || distance < 1e-6)
        return {};
    // 防止鼠标越过地平线时产生无限量补点。
    const qreal requested = std::ceil(distance / qMax(.5, m_diameter * .12));
    if (requested > 2048 || !PerspectiveTransform::mapsDomain(m_targetMapping.forward(), QRectF(m_lastPosition, position).normalized()))
        return {};
    const int count = int(requested);
    QRect dirty;
    for (int i = 1; i <= count; ++i)
        dirty = dirty.united(applyDab(layer, m_lastPosition + (position - m_lastPosition) * (qreal(i) / count)));
    m_lastPosition = position;
    return dirty;
}

void CloneTool::end()
{
    m_stamp.source = QImage();
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
    const StampContext preview = makeStampContext(source, targetMapping.forward(),
                                                   m_sourceMapping.forward(), offset);
    const QRect area = dabRect(preview, position);
    if (area.isEmpty())
        return;
    QImage dab(area.size(), QImage::Format_ARGB32_Premultiplied);
    dab.fill(Qt::transparent);
    renderDab(dab, area, position, preview);
    painter.drawImage(area.topLeft(), dab);
}

CloneTool::StampContext CloneTool::makeStampContext(const QImage &source,
                                                    const QTransform &targetToCanvas,
                                                    const QTransform &sourceToCanvas,
                                                    const QPointF &offset)
{
    StampContext context;
    bool invertible = false;
    context.canvasToTarget = targetToCanvas.inverted(&invertible);
    context.source = invertible ? source.convertToFormat(QImage::Format_ARGB32_Premultiplied) : QImage();
    context.targetToCanvas = targetToCanvas;
    context.sourceToCanvas = sourceToCanvas;
    context.offset = offset;
    return context;
}

QRect CloneTool::dabRect(const StampContext &context, const QPointF &position) const
{
    if (context.source.isNull())
        return {};
    const qreal radius = m_diameter / 2.0;
    const QRectF bounds(position.x() - radius, position.y() - radius, m_diameter, m_diameter);
    if (!PerspectiveTransform::mapsDomain(context.targetToCanvas, bounds))
        return {};
    return context.targetToCanvas.mapRect(bounds)
        .intersected(QRectF(context.source.rect())).toAlignedRect();
}

bool CloneTool::renderDab(QImage &dab, const QRect &area, const QPointF &position,
                          const StampContext &context) const
{
    if (context.source.isNull() || dab.isNull() || area.isEmpty())
        return false;
    const qreal radius = m_diameter / 2.0;
    const qreal hardness = m_hardness / 100.0;
    const qreal opacity = m_opacity / 100.0;
    bool changed = false;
    for (int y = 0; y < area.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(dab.scanLine(y));
        for (int x = 0; x < area.width(); ++x) {
            QPointF target;
            if (!PerspectiveTransform::mapPoint(context.canvasToTarget, QPointF(area.x() + x + .5, area.y() + y + .5), &target))
                continue;
            const qreal distance = QLineF(target, position).length() / radius;
            if (!(distance < 1))
                continue;
            QPointF source;
            if (!PerspectiveTransform::mapPoint(context.sourceToCanvas, target + context.offset, &source))
                continue;
            if (!(source.x() >= 0 && source.y() >= 0 &&
                  source.x() < context.source.width() && source.y() < context.source.height()))
                continue;
            const qreal mask = distance <= hardness ? 1 : (1 - distance) / (1 - hardness);
            row[x] = sample(context.source, source, mask * opacity);
            changed |= qAlpha(row[x]) > 0;
        }
    }
    return changed;
}

QRect CloneTool::applyDab(QImage &layer, const QPointF &position)
{
    const QRect area = dabRect(m_stamp, position);
    if (area.isEmpty())
        return {};
    QImage dab(area.size(), QImage::Format_ARGB32_Premultiplied);
    dab.fill(Qt::transparent);
    if (!renderDab(dab, area, position, m_stamp))
        return {};
    QPainter painter(&layer);
    painter.drawImage(area.topLeft(), dab);
    return area;
}
