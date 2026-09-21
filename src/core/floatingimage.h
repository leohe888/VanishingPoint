#pragma once

#include "perspectivefacet.h"

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
    QVector<PerspectiveFacet> surfaceFacets;
    int hostFacetIndex = -1;

    QSizeF displaySize() const;
    QTransform bitmapToPlacementTransform() const;
    QVector<QPointF> transformHandlePositions() const;

    QPointF mapPlacementToCanvas(const QPointF &point, int *facetIndex = nullptr) const;
    bool mapCanvasToPlacement(const QPointF &point, QPointF *result,
                              int fallbackFacetIndex = -1) const;
};
