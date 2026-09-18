#include "clonestampengine.h"

#include <QLineF>
#include <QPainter>
#include <QtMath>
#include <cmath>

namespace {
// 双线性取样。源图已转成预乘 alpha，按权重平均即可，透明边缘不会带入黑色。
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
}

QRect CloneStampEngine::beginStroke(QImage &layer, const QImage &source,
                                    const QTransform &targetToCanvas,
                                    const QTransform &sourceToCanvas,
                                    const QPointF &offset, const QPointF &position)
{
    bool invertible = false;
    m_canvasToTarget = targetToCanvas.inverted(&invertible);
    m_source = invertible ? source.convertToFormat(QImage::Format_ARGB32_Premultiplied) : QImage();
    m_targetToCanvas = targetToCanvas;
    m_sourceToCanvas = sourceToCanvas;
    m_offset = offset;
    m_lastPosition = position;
    return applyDab(layer, position);
}

QRect CloneStampEngine::drawStrokeTo(QImage &layer, const QPointF &position)
{
    const qreal distance = QLineF(m_lastPosition, position).length();
    if (!std::isfinite(distance) || distance < 1e-6)
        return {};
    // 防止鼠标越过地平线时产生无限量补点。
    const int count = int(qMin(10000.0, std::ceil(distance / qMax(.5, m_diameter * .12))));
    QRect dirty;
    for (int i = 1; i <= count; ++i)
        dirty = dirty.united(applyDab(layer, m_lastPosition + (position - m_lastPosition) * (qreal(i) / count)));
    m_lastPosition = position;
    return dirty;
}

void CloneStampEngine::setPreview(const QImage &source, const QTransform &targetToCanvas,
                                  const QTransform &sourceToCanvas, const QPointF &offset)
{
    bool invertible = false;
    m_canvasToTarget = targetToCanvas.inverted(&invertible);
    m_source = invertible ? source.convertToFormat(QImage::Format_ARGB32_Premultiplied) : QImage();
    m_targetToCanvas = targetToCanvas;
    m_sourceToCanvas = sourceToCanvas;
    m_offset = offset;
    // 预览不写 m_lastPosition，也不进入落笔状态
}

QRect CloneStampEngine::dabRect(const QPointF &position) const
{
    if (m_source.isNull())
        return {};
    const qreal radius = m_diameter / 2.0;
    const QRectF bounds(position.x() - radius, position.y() - radius, m_diameter, m_diameter);
    // 裁到源图（画布同尺寸）范围，画面之外的部分不留笔触
    return m_targetToCanvas.mapRect(bounds).intersected(QRectF(m_source.rect())).toAlignedRect();
}

bool CloneStampEngine::renderDab(QImage &dab, const QRect &area, const QPointF &position) const
{
    if (m_source.isNull() || dab.isNull() || area.isEmpty())
        return false;
    const qreal radius = m_diameter / 2.0;
    const qreal hardness = m_hardness / 100.0; // 半径内完全不透明的比例
    const qreal opacity = m_opacity / 100.0;
    bool changed = false;
    for (int y = 0; y < area.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(dab.scanLine(y));
        for (int x = 0; x < area.width(); ++x) {
            const QPointF target = m_canvasToTarget.map(QPointF(area.x() + x + .5, area.y() + y + .5));
            const qreal distance = QLineF(target, position).length() / radius;
            if (!(distance < 1)) // NaN 也在此处被挡下
                continue;
            const QPointF source = m_sourceToCanvas.map(target + m_offset);
            // 取反的合取式同时排除了坐标非有限的情形
            if (!(source.x() >= 0 && source.y() >= 0 &&
                  source.x() < m_source.width() && source.y() < m_source.height()))
                continue;
            const qreal mask = distance <= hardness ? 1 : (1 - distance) / (1 - hardness);
            row[x] = sample(m_source, source, mask * opacity);
            changed |= qAlpha(row[x]) > 0;
        }
    }
    return changed;
}

QRect CloneStampEngine::applyDab(QImage &layer, const QPointF &position)
{
    const QRect area = dabRect(position);
    if (area.isEmpty())
        return {};
    QImage dab(area.size(), QImage::Format_ARGB32_Premultiplied);
    dab.fill(Qt::transparent);
    if (!renderDab(dab, area, position))
        return {};
    QPainter painter(&layer);
    painter.drawImage(area.topLeft(), dab);
    return area;
}
