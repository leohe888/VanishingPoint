#include "floatingimageprojection.h"

#include <QCache>
#include <QDataStream>
#include <QIODevice>
#include <QPainterPathStroker>

QByteArray FloatingImageProjection::cacheKey(const FloatingImage &image)
{
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << image.bitmap.size() << image.placementOrigin << image.scaleFactors
           << image.rotationDegrees << image.surfaceAttached << image.hostQuadIndex
           << qint64(image.surfaceQuads.size());
    for (const PerspectiveQuad &quad : image.surfaceQuads) {
        for (int corner = 0; corner < PerspectiveQuad::CornerCount; ++corner)
            stream << quad.canvasCorners()[corner] << quad.surfaceCorners()[corner];
    }
    return key;
}

QSharedPointer<const FloatingImageProjection>
FloatingImageProjection::forImage(const FloatingImage &image)
{
    static thread_local QCache<QByteArray, QSharedPointer<const FloatingImageProjection>> cache(128);
    const QByteArray key = cacheKey(image);
    if (const auto *cached = cache.object(key))
        return *cached;

    QSharedPointer<const FloatingImageProjection> projection(new FloatingImageProjection(image));
    cache.insert(key, new QSharedPointer<const FloatingImageProjection>(projection));
    return projection;
}

FloatingImageProjection::FloatingImageProjection(const FloatingImage &image)
    : m_bitmapToPlacement(image.bitmapToPlacementTransform()),
      m_placementOrigin(image.placementOrigin)
{
    if (image.bitmap.isNull())
        return;

    QPainterPath bitmapPath;
    bitmapPath.addRect(QRectF(QPointF(0, 0), QSizeF(image.bitmap.size())));
    auto appendPatch = [this](const QPolygonF &bitmapDomain, const QPolygonF &canvasTarget,
                              const QPainterPath &bitmapClip) {
        const PerspectiveTransform bitmapToCanvas(bitmapDomain, canvasTarget);
        if (!bitmapClip.isEmpty() && bitmapToCanvas.isValid()) {
            m_patches.append({bitmapToCanvas, bitmapClip,
                              bitmapToCanvas.forward().map(bitmapClip)});
        }
    };

    if (!image.surfaceAttached || image.surfaceQuads.isEmpty()) {
        const QRectF rect(QPointF(0, 0), QSizeF(image.bitmap.size()));
        const QPolygonF bitmapDomain{
            rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()
        };
        appendPatch(bitmapDomain, m_bitmapToPlacement.map(bitmapDomain), bitmapPath);
    } else {
        QVector<QPolygonF> bitmapDomains;
        QVector<QPainterPath> quadClips;
        QPainterPath hostClip = bitmapPath;
        const QTransform placementToBitmap = m_bitmapToPlacement.inverted();
        for (int index = 0; index < image.surfaceQuads.size(); ++index) {
            const QPolygonF bitmapDomain =
                placementToBitmap.map(image.surfaceQuads[index].surfacePolygon());
            bitmapDomains.append(bitmapDomain);
            QPainterPath quadPath;
            quadPath.addPolygon(bitmapDomain);
            quadPath.closeSubpath();
            quadClips.append(bitmapPath.intersected(quadPath));
            if (index != image.hostQuadIndex)
                hostClip = hostClip.subtracted(quadPath);
        }
        if (image.hostQuadIndex >= 0 && image.hostQuadIndex < image.surfaceQuads.size()) {
            appendPatch(bitmapDomains[image.hostQuadIndex],
                        image.surfaceQuads[image.hostQuadIndex].canvasPolygon(), hostClip);
        }
        for (int index = 0; index < image.surfaceQuads.size(); ++index) {
            if (index != image.hostQuadIndex) {
                appendPatch(bitmapDomains[index], image.surfaceQuads[index].canvasPolygon(),
                            quadClips[index]);
            }
        }
    }

    if (m_patches.size() == 1) {
        m_canvasOutline = m_patches.first().canvasClip;
    } else {
        QPainterPathStroker seamTolerance;
        seamTolerance.setWidth(.0001);
        seamTolerance.setJoinStyle(Qt::MiterJoin);
        for (const ProjectedImagePatch &patch : m_patches) {
            m_canvasOutline = m_canvasOutline.united(
                patch.canvasClip.united(seamTolerance.createStroke(patch.canvasClip)));
        }
        m_canvasOutline = m_canvasOutline.simplified();
    }

    for (const QPointF &position : image.transformHandlePositions())
        m_transformHandles.append(image.mapPlacementToCanvas(position));
}

bool FloatingImageProjection::hitTest(const QPointF &canvasPoint,
                                      QPointF *placementOffset) const
{
    for (auto patch = m_patches.crbegin(); patch != m_patches.crend(); ++patch) {
        QPointF bitmapPoint;
        if (!patch->bitmapToCanvas.mapInverse(canvasPoint, &bitmapPoint)
            || !patch->bitmapClip.contains(bitmapPoint)) {
            continue;
        }
        if (placementOffset)
            *placementOffset = m_bitmapToPlacement.map(bitmapPoint) - m_placementOrigin;
        return true;
    }
    return false;
}
