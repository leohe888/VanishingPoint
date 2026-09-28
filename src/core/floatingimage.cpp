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

QPointF FloatingImage::mapPlacementToCanvas(const QPointF &point, int *quadIndex) const
{
    int selectedQuad = hostQuadIndex;
    if (surfaceAttached) {
        for (int index = surfaceQuads.size() - 1; index >= 0; --index) {
            if (surfaceQuads[index].surfacePolygon().containsPoint(point, Qt::OddEvenFill)) {
                selectedQuad = index;
                break;
            }
        }
    }
    if (quadIndex)
        *quadIndex = selectedQuad;
    if (!surfaceAttached || selectedQuad < 0 || selectedQuad >= surfaceQuads.size())
        return point;

    QPointF canvasPoint;
    if (!surfaceQuads[selectedQuad].surfaceToCanvasTransform().mapForward(point, &canvasPoint))
        return QPointF(qQNaN(), qQNaN());
    return canvasPoint;
}

bool FloatingImage::mapCanvasToPlacement(const QPointF &point, QPointF *result,
                                         int fallbackQuadIndex) const
{
    if (!result)
        return false;
    if (!surfaceAttached || surfaceQuads.isEmpty()) {
        *result = point;
        return true;
    }

    int selectedQuad = fallbackQuadIndex >= 0 ? fallbackQuadIndex : hostQuadIndex;
    for (int index = surfaceQuads.size() - 1; index >= 0; --index) {
        if (surfaceQuads[index].containsCanvasPoint(point)) {
            selectedQuad = index;
            break;
        }
    }
    if (selectedQuad < 0 || selectedQuad >= surfaceQuads.size())
        return false;

    QPointF surfacePoint;
    const bool mapped = surfaceQuads[selectedQuad].surfaceToCanvasTransform()
                            .mapInverse(point, &surfacePoint);
    *result = surfacePoint;
    return mapped && std::isfinite(result->x()) && std::isfinite(result->y());
}
