#pragma once

#include "floatingimage.h"

#include <QPainterPath>
#include <QSharedPointer>

struct ProjectedImagePatch
{
    PerspectiveTransform bitmapToCanvas;
    QPainterPath bitmapClip;
    QPainterPath canvasClip;
};

// Cached, immutable canvas projection used by rendering, handles and hit testing.
class FloatingImageProjection
{
public:
    static QByteArray cacheKey(const FloatingImage &image);
    static QSharedPointer<const FloatingImageProjection> forImage(const FloatingImage &image);

    const QVector<ProjectedImagePatch> &patches() const { return m_patches; }
    const QPainterPath &canvasOutline() const { return m_canvasOutline; }
    const QVector<QPointF> &transformHandles() const { return m_transformHandles; }
    bool hitTest(const QPointF &canvasPoint, QPointF *placementOffset = nullptr) const;

private:
    explicit FloatingImageProjection(const FloatingImage &image);

    QVector<ProjectedImagePatch> m_patches;
    QPainterPath m_canvasOutline;
    QVector<QPointF> m_transformHandles;
    QTransform m_bitmapToPlacement;
    QPointF m_placementOrigin;
};
