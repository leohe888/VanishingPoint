#pragma once

#include "floatingselection.h"

#include <QPainterPath>
#include <QSharedPointer>

struct ProjectedSelectionPatch
{
    PerspectiveTransform bitmapToCanvas;
    QPainterPath bitmapClip;
    QPainterPath canvasClip;
};

// Cached, immutable canvas projection used by rendering, handles and hit testing.
class FloatingSelectionProjection
{
public:
    static QByteArray cacheKey(const FloatingSelection &image);
    static QSharedPointer<const FloatingSelectionProjection> forSelection(const FloatingSelection &image);

    const QVector<ProjectedSelectionPatch> &patches() const { return m_patches; }
    const QPainterPath &canvasOutline() const { return m_canvasOutline; }
    const QPainterPath &canvasPaintOutline() const { return m_canvasPaintOutline; }
    const QVector<QPointF> &transformHandles() const { return m_transformHandles; }
    bool hitTest(const QPointF &canvasPoint, QPointF *placementOffset = nullptr) const;

private:
    explicit FloatingSelectionProjection(const FloatingSelection &image);

    QVector<ProjectedSelectionPatch> m_patches;
    QPainterPath m_canvasOutline;
    QPainterPath m_canvasPaintOutline;
    QRectF m_bitmapSelectionRect;
    QVector<QPointF> m_transformHandles;
    QTransform m_bitmapToPlacement;
    QPointF m_placementOrigin;
};
