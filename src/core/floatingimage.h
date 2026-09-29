#pragma once

#include "perspectivequad.h"

#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QTransform>
#include <QVector>

// A bitmap that remains independently editable until it is baked into the paint layer.
// placementOrigin is expressed in canvas coordinates while detached, and in unfolded
// surface coordinates while attached.
struct FloatingImage
{
    QImage bitmap;
    QPointF placementOrigin;
    QPointF scaleFactors = QPointF(1, 1);
    qreal rotationDegrees = 0;
    bool surfaceAttached = false;
    QVector<PerspectiveQuad> surfaceQuads;
    int hostQuadIndex = -1;

    friend bool operator==(const FloatingImage &a, const FloatingImage &b)
    {
        return a.bitmap == b.bitmap && a.placementOrigin == b.placementOrigin
            && a.scaleFactors == b.scaleFactors && a.rotationDegrees == b.rotationDegrees
            && a.surfaceAttached == b.surfaceAttached && a.surfaceQuads == b.surfaceQuads
            && a.hostQuadIndex == b.hostQuadIndex;
    }
    QSizeF displaySize() const;
    QTransform bitmapToPlacementTransform() const;
    QVector<QPointF> transformHandlePositions() const;

    QPointF mapPlacementToCanvas(const QPointF &point, int *quadIndex = nullptr) const;
    bool mapCanvasToPlacement(const QPointF &point, QPointF *result,
                              int fallbackQuadIndex = -1) const;
};
