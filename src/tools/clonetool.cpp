#include "clonetool.h"

#include <QLineF>
#include <QPainter>
#include <QtMath>
#include <QDataStream>
#include <QIODevice>

#include <cmath>

namespace {

// 源图使用预乘 alpha，双线性取样时透明边缘不会带入黑色。
QRgb sample(const uchar *pixels, int width, int height, int stride, const QPointF &point, qreal coverage)
{
    const qreal x = point.x() - .5, y = point.y() - .5;
    const int x0 = qFloor(x), y0 = qFloor(y);
    const qreal fx = x - x0, fy = y - y0;
    qreal r = 0, g = 0, b = 0, a = 0;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const int sx = x0 + i, sy = y0 + j;
            if (sx < 0 || sy < 0 || sx >= width || sy >= height)
                continue;
            const QRgb pixel = reinterpret_cast<const QRgb *>(pixels + qint64(sy) * stride)[sx];
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
    const PerspectiveTransform mapping = quad.surfaceToCanvasTransform();
    QPointF surface;
    if (!mapping.mapInverse(point, &surface))
        return false;
    m_sourceMapping = mapping;
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
                       const QSize &canvasSize, const QPointF &point,
                       const std::function<void(const QRect &)> &prepare)
{
    if (!m_hasSource || layer.isNull())
        return {};
    m_targetMapping = targetAt(planes, canvasSize, point);
    QPointF position;
    if (!m_targetMapping.mapInverse(point, &position))
        return {};
    m_offset = anchoredOffset(position);
    m_hasOffset = m_drawing = true;
    m_stamp = makeStampContext(source, m_targetMapping, m_sourceMapping, m_offset);
    m_lastPosition = position;
    m_healDabs.clear();
    m_healDirty = {};
    m_beforeHealStroke = m_healMode != HealingBlend::Off ? layer : QImage();
    return applyDab(layer, position, prepare);
}

QRect CloneTool::move(QImage &layer, const QPointF &point, const std::function<void(const QRect &)> &prepare)
{
    QPointF position;
    if (!m_drawing || !m_targetMapping.mapInverse(point, &position))
        return {};
    const qreal distance = QLineF(m_lastPosition, position).length();
    if (!std::isfinite(distance) || distance < 1e-6)
        return {};
    // 防止鼠标越过地平线时产生无限量补点。
    const qreal requested = std::ceil(distance / qMax(.5, m_diameter * .12));
    if (requested > 2048 || !m_targetMapping.canProject(QRectF(m_lastPosition, position).normalized()))
        return {};
    const int count = int(requested);
    QRect dirty;
    for (int i = 1; i <= count; ++i)
        dirty = dirty.united(applyDab(layer, m_lastPosition + (position - m_lastPosition) * (qreal(i) / count), prepare));
    m_lastPosition = position;
    return dirty;
}

QRect CloneTool::end(QImage *layer)
{
    QRect dirty;
    if (layer && m_drawing && !m_beforeHealStroke.isNull() && !m_healDabs.isEmpty()) {
        dirty = m_healDirty.intersected(layer->rect());
        {
            QPainter painter(layer);
            if (!m_selectionClip.isEmpty()) painter.setClipPath(m_selectionClip);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(dirty, m_beforeHealStroke, dirty);
        }
        const Dab settings{{}, m_diameter, m_hardness, m_opacity};
        m_finalizingHeal = true;
        for (const Dab &dab : m_healDabs) {
            m_diameter = dab.diameter; m_hardness = dab.hardness; m_opacity = dab.opacity;
            applyDab(*layer, dab.position, {});
        }
        m_finalizingHeal = false;
        m_diameter = settings.diameter; m_hardness = settings.hardness; m_opacity = settings.opacity;
    }
    m_beforeHealStroke = QImage();
    m_healDabs.clear();
    m_healDirty = {};
    m_stamp.source = QImage();
    m_dab = QImage();
    m_drawing = false;
    if (!m_aligned) {
        m_hasOffset = false;
        m_marker = originalMarker();
    }
    return dirty;
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
    const StampContext preview = makeStampContext(source, targetMapping, m_sourceMapping, offset);
    const QRect area = dabRect(preview, position);
    if (area.isEmpty())
        return;
    if (m_previewDab.size() != area.size()) m_previewDab = QImage(area.size(), QImage::Format_ARGB32_Premultiplied);
    m_previewDab.fill(Qt::transparent);
    renderDab(m_previewDab, area, position, preview);
    painter.drawImage(area.topLeft(), m_previewDab);
}

QRectF CloneTool::previewBounds(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                               const QPointF &point) const
{
    if (!m_hasSource) return {};
    const PerspectiveTransform mapping = targetAt(planes, canvasSize, point);
    QPointF position;
    if (!mapping.mapInverse(point, &position)) return {};
    const qreal radius = m_diameter / 2.0;
    const QRectF bounds(position - QPointF(radius, radius), QSizeF(m_diameter, m_diameter));
    return mapping.canProject(bounds) ? mapping.forward().mapRect(bounds).intersected(QRectF(QPointF(), QSizeF(canvasSize))) : QRectF();
}

CloneTool::StampContext CloneTool::makeStampContext(const QImage &source,
                                                    const PerspectiveTransform &targetToCanvas,
                                                    const PerspectiveTransform &sourceToCanvas,
                                                    const QPointF &offset)
{
    StampContext context;
    context.source = targetToCanvas.isValid() && sourceToCanvas.isValid()
        ? source.convertToFormat(QImage::Format_ARGB32_Premultiplied) : QImage();
    context.targetToCanvas = targetToCanvas;
    context.sourceToCanvas = sourceToCanvas;
    context.offset = offset;
    QDataStream key(&context.coordinatesKey, QIODevice::WriteOnly);
    key << targetToCanvas.forward() << sourceToCanvas.forward() << offset;
    return context;
}

QRect CloneTool::dabRect(const StampContext &context, const QPointF &position) const
{
    if (context.source.isNull())
        return {};
    const qreal radius = m_diameter / 2.0;
    const QRectF bounds(position.x() - radius, position.y() - radius, m_diameter, m_diameter);
    if (!context.targetToCanvas.canProject(bounds))
        return {};
    return context.targetToCanvas.forward().mapRect(bounds)
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
    if (context.coordinatesKey != m_coordinatesKey) {
        m_coordinateTiles.clear(); m_coordinatesKey = context.coordinatesKey;
    }
    const int tileColumns = area.right()/CoordinateTileSize - area.left()/CoordinateTileSize + 1;
    const int tileRows = area.bottom()/CoordinateTileSize - area.top()/CoordinateTileSize + 1;
    const bool cacheCoordinates = qint64(tileColumns)*tileRows <= CoordinateTileCount;
    const uchar *pixels = context.source.constBits();
    const int width = context.source.width(), height = context.source.height(), stride = context.source.bytesPerLine();
    const HealingBlend heal(m_finalizingHeal ? m_healMode : HealingBlend::Off, context.source, context.targetToCanvas,
                            position, radius, {}, &context.sourceToCanvas, context.offset);
    for (int y = 0; y < area.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(dab.scanLine(y));
        CoordinateTile *tile = nullptr;
        int previousColumn = -1;
        for (int x = 0; x < area.width(); ++x) {
            const int canvasX = area.x()+x, canvasY = area.y()+y;
            const int index = (canvasY%CoordinateTileSize)*CoordinateTileSize + canvasX%CoordinateTileSize;
            if (cacheCoordinates && previousColumn != canvasX/CoordinateTileSize) {
                previousColumn = canvasX/CoordinateTileSize;
                const quint64 key = (quint64(canvasY/CoordinateTileSize)<<32) | quint32(previousColumn);
                tile = m_coordinateTiles.object(key);
                if (!tile) { tile = new CoordinateTile; m_coordinateTiles.insert(key, tile); }
            }
            QPointF target;
            if (tile) {
                if (tile->state[index]==0) {
                    tile->state[index] = context.targetToCanvas.mapInverse(QPointF(canvasX+.5,canvasY+.5), &tile->target[index]) ? 2 : 1;
                }
                if (tile->state[index]==1) continue;
                target = tile->target[index];
            } else if (!context.targetToCanvas.mapInverse(QPointF(canvasX+.5,canvasY+.5), &target)) continue;
            const qreal distance = QLineF(target, position).length() / radius;
            if (!(distance < 1))
                continue;
            QPointF source;
            if (tile) {
                if (tile->state[index]==2)
                    tile->state[index] = context.sourceToCanvas.mapForward(target+context.offset, &tile->source[index]) ? 3 : 4;
                if (tile->state[index]==4) continue;
                source = tile->source[index];
            } else if (!context.sourceToCanvas.mapForward(target + context.offset, &source)) continue;
            if (!(source.x() >= 0 && source.y() >= 0 &&
                  source.x() < context.source.width() && source.y() < context.source.height()))
                continue;
            const qreal mask = distance <= hardness ? 1 : (1 - distance) / (1 - hardness);
            row[x] = heal.blend(sample(pixels, width, height, stride, source, mask * opacity), target);
            changed |= qAlpha(row[x]) > 0;
        }
    }
    return changed;
}

QRect CloneTool::applyDab(QImage &layer, const QPointF &position, const std::function<void(const QRect &)> &prepare)
{
    QRect area = dabRect(m_stamp, position);
    if (!m_selectionClip.isEmpty()) area = area.intersected(m_selectionClip.boundingRect().toAlignedRect());
    if (area.isEmpty())
        return {};
    if (m_dab.size() != area.size()) m_dab = QImage(area.size(), QImage::Format_ARGB32_Premultiplied);
    m_dab.fill(Qt::transparent);
    if (!renderDab(m_dab, area, position, m_stamp))
        return {};
    if (prepare) prepare(area);
    QPainter painter(&layer);
    if (!m_selectionClip.isEmpty()) painter.setClipPath(m_selectionClip);
    painter.drawImage(area.topLeft(), m_dab);
    if (!m_finalizingHeal && !m_beforeHealStroke.isNull()) {
        m_healDabs.append({position, m_diameter, m_hardness, m_opacity});
        m_healDirty = m_healDirty.united(area);
    }
    return area;
}

void CloneTool::resetSource()
{
    end();
    m_previewDab = QImage();
    m_coordinateTiles.clear(); m_coordinatesKey.clear();
    m_hasSource = m_hasOffset = false;
    m_sourceMapping = m_targetMapping = PerspectiveTransform();
    m_source = m_offset = m_marker = QPointF();
}
