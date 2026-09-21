#include "floatingimage.h"

#include <QtMath>

#include <cmath>

QSizeF FloatingImage::displaySize() const
{
    return QSizeF(bitmap.width() * scaleFactors.x(), bitmap.height() * scaleFactors.y());
}

QTransform FloatingImage::bitmapToPlacementTransform() const
{
    const QSizeF size = displaySize();
    const QPointF center = placementOrigin + QPointF(size.width() / 2, size.height() / 2);
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.rotate(rotationDegrees);
    transform.translate(-size.width() / 2, -size.height() / 2);
    transform.scale(scaleFactors.x(), scaleFactors.y());
    return transform;
}

QVector<QPointF> FloatingImage::transformHandlePositions() const
{
    const QRectF rect(QPointF(0, 0), QSizeF(bitmap.size()));
    QVector<QPointF> positions{
        rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft(),
        (rect.topLeft() + rect.topRight()) / 2,
        (rect.topRight() + rect.bottomRight()) / 2,
        (rect.bottomLeft() + rect.bottomRight()) / 2,
        (rect.topLeft() + rect.bottomLeft()) / 2
    };
    const QTransform transform = bitmapToPlacementTransform();
    for (QPointF &position : positions)
        position = transform.map(position);
    return positions;
}

QPointF FloatingImage::mapPlacementToCanvas(const QPointF &point, int *facetIndex) const
{
    int selectedFacet = hostFacetIndex;
    if (surfaceAttached) {
        for (int index = surfaceFacets.size() - 1; index >= 0; --index) {
            if (surfaceFacets[index].surfacePolygon().containsPoint(point, Qt::OddEvenFill)) {
                selectedFacet = index;
                break;
            }
        }
    }
    if (facetIndex)
        *facetIndex = selectedFacet;
    if (!surfaceAttached || selectedFacet < 0 || selectedFacet >= surfaceFacets.size())
        return point;

    QPointF canvasPoint;
    if (!surfaceFacets[selectedFacet].surfaceToCanvasTransform().mapForward(point, &canvasPoint))
        return QPointF(qQNaN(), qQNaN());
    return canvasPoint;
}

bool FloatingImage::mapCanvasToPlacement(const QPointF &point, QPointF *result,
                                         int fallbackFacetIndex) const
{
    if (!result)
        return false;
    if (!surfaceAttached || surfaceFacets.isEmpty()) {
        *result = point;
        return true;
    }

    int selectedFacet = fallbackFacetIndex >= 0 ? fallbackFacetIndex : hostFacetIndex;
    for (int index = surfaceFacets.size() - 1; index >= 0; --index) {
        if (surfaceFacets[index].containsCanvasPoint(point)) {
            selectedFacet = index;
            break;
        }
    }
    if (selectedFacet < 0 || selectedFacet >= surfaceFacets.size())
        return false;

    bool mapped = false;
    *result = surfaceFacets[selectedFacet].mapCanvasToSurface(point, &mapped);
    return mapped && std::isfinite(result->x()) && std::isfinite(result->y());
}
