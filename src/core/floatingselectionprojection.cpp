#include "floatingselectionprojection.h"

#include <QCache>
#include <QDataStream>
#include <QIODevice>
#include <QPainterPathStroker>

QByteArray FloatingSelectionProjection::cacheKey(const FloatingSelection &image)
{
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << image.bitmap.size() << image.placementOrigin << image.scaleFactors
           << image.selectionRectInBitmap
           << image.rotationDegrees << image.surfaceAttached << image.hostQuadIndex
           << qint64(image.surfaceQuads.size());
    for (const PerspectiveQuad &quad : image.surfaceQuads) {
        for (int corner = 0; corner < PerspectiveQuad::CornerCount; ++corner)
            stream << quad.canvasCorners()[corner] << quad.surfaceCorners()[corner];
    }
    return key;
}

QSharedPointer<const FloatingSelectionProjection>
FloatingSelectionProjection::forSelection(const FloatingSelection &image)
{
    static thread_local QCache<QByteArray, QSharedPointer<const FloatingSelectionProjection>> cache(128);
    const QByteArray key = cacheKey(image);
    if (const auto *cached = cache.object(key))
        return *cached;

    QSharedPointer<const FloatingSelectionProjection> projection(new FloatingSelectionProjection(image));
    cache.insert(key, new QSharedPointer<const FloatingSelectionProjection>(projection));
    return projection;
}

FloatingSelectionProjection::FloatingSelectionProjection(const FloatingSelection &image)
    : m_bitmapSelectionRect(image.bitmapSelectionRect()),
      m_bitmapToPlacement(image.bitmapToPlacementTransform()),
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
        const QPolygonF bitmapDomain(QVector<QPointF>{
            rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()
        });
        appendPatch(bitmapDomain, m_bitmapToPlacement.map(bitmapDomain), bitmapPath);
    } else {
        QVector<QPolygonF> bitmapDomains;
        QVector<QPainterPath> quadClips;
        QPainterPath logicalPath;
        logicalPath.addRect(m_bitmapSelectionRect);
        bool overlapsSurface = false;
        const QTransform placementToBitmap = m_bitmapToPlacement.inverted();
        for (const PerspectiveQuad &quad : image.surfaceQuads) {
            const QPolygonF bitmapDomain = placementToBitmap.map(quad.surfacePolygon());
            bitmapDomains.append(bitmapDomain);
            QPainterPath quadPath;
            quadPath.addPolygon(bitmapDomain);
            quadPath.closeSubpath();
            const QPainterPath clip = bitmapPath.intersected(quadPath);
            quadClips.append(clip);
            overlapsSurface = overlapsSurface || !clip.intersected(logicalPath).isEmpty();
        }
        const int host = image.hostQuadIndex;
        if (host >= 0 && host < image.surfaceQuads.size()) {
            if (!overlapsSurface) {
                // Outside the entire surface group, show the complete floating image.
                // Keep the host perspective and bitmap so re-entry is reversible.
                appendPatch(bitmapDomains[host], image.surfaceQuads[host].canvasPolygon(), bitmapPath);
            } else {
                // Neighbours override the host; later neighbours override earlier ones.
                // Give each bitmap region one owner to avoid double opacity at overlaps.
                QVector<QPainterPath> ownedClips(quadClips.size());
                QPainterPath covered;
                for (int index = quadClips.size() - 1; index >= 0; --index) {
                    if (index == host) continue;
                    ownedClips[index] = quadClips[index].subtracted(covered);
                    covered = covered.united(quadClips[index]);
                }
                ownedClips[host] = quadClips[host].subtracted(covered);
                appendPatch(bitmapDomains[host], image.surfaceQuads[host].canvasPolygon(), ownedClips[host]);
                for (int index = 0; index < image.surfaceQuads.size(); ++index) {
                    if (index != host)
                        appendPatch(bitmapDomains[index], image.surfaceQuads[index].canvasPolygon(), ownedClips[index]);
                }
            }
        }
    }

    const auto outlineFor = [this](bool logical) {
        QPainterPath outline;
        QPainterPath logicalPath;
        logicalPath.addRect(m_bitmapSelectionRect);
        QPainterPathStroker seamTolerance;
        seamTolerance.setWidth(.0001);
        seamTolerance.setJoinStyle(Qt::MiterJoin);
        for (const ProjectedSelectionPatch &patch : m_patches) {
            const QPainterPath clip = logical
                ? patch.bitmapToCanvas.forward().map(patch.bitmapClip.intersected(logicalPath))
                : patch.canvasClip;
            if (m_patches.size() == 1) return clip;
            outline = outline.united(clip.united(seamTolerance.createStroke(clip)));
        }
        return outline.simplified();
    };
    const auto closedOutline = [](const QPainterPath &path) {
        QPainterPath closed;
        closed.setFillRule(path.fillRule());
        // Qt's path intersections can return a filled contour without a closing line.
        // Fill/hit testing implicitly close it; both guide renderers need that final edge explicitly.
        for (const QPolygonF &polygon : path.toSubpathPolygons()) {
            closed.addPolygon(polygon);
            closed.closeSubpath();
        }
        return closed;
    };
    m_canvasOutline = closedOutline(outlineFor(true));
    m_canvasPaintOutline = closedOutline(outlineFor(false));

    for (const QPointF &position : image.transformHandlePositions())
        m_transformHandles.append(image.mapPlacementToCanvas(position));
}

bool FloatingSelectionProjection::hitTest(const QPointF &canvasPoint,
                                      QPointF *placementOffset) const
{
    for (auto patch = m_patches.crbegin(); patch != m_patches.crend(); ++patch) {
        QPointF bitmapPoint;
        if (!patch->bitmapToCanvas.mapInverse(canvasPoint, &bitmapPoint)
            || !m_bitmapSelectionRect.contains(bitmapPoint)
            || !patch->bitmapClip.contains(bitmapPoint)) {
            continue;
        }
        if (placementOffset)
            *placementOffset = m_bitmapToPlacement.map(bitmapPoint) - m_placementOrigin;
        return true;
    }
    return false;
}
