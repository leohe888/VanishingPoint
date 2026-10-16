#pragma once

#include "perspectivequad.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <QVector>

// A bitmap that remains independently editable until it is baked into the paint layer.
// placementOrigin is expressed in canvas coordinates while detached, and in unfolded
// surface coordinates while attached.
struct FloatingSelection
{
    // 选区形状由蒙版灰度保存在 alpha 中，输出裁到有效像素的外接矩形。
    static QImage copyPixels(const QImage &source, const QImage &selectionMask);
    QImage bitmap;
    QImage unfeatheredBitmap; // 保留原始 alpha，反复调节羽化不累积损失。
    int feather = 0;
    void setFeather(int value);
    QImage unhealedBitmap; // 修复前的像素，移动后重新融合，避免重复修复累积。
    int opacity = 100;
    int healMode = 0;
    QPointF placementOrigin;
    QPointF scaleFactors = QPointF(1, 1);
    QRectF selectionRectInBitmap; // 原选框范围；羽化的外侧像素不参与控制框几何。
    qreal rotationDegrees = 0;
    bool flippedHorizontally = false;
    bool flippedVertically = false;
    bool surfaceAttached = false;
    QVector<PerspectiveQuad> surfaceQuads;
    int hostQuadIndex = -1;

    friend bool operator==(const FloatingSelection &a, const FloatingSelection &b)
    {
        return a.bitmap == b.bitmap && a.unhealedBitmap == b.unhealedBitmap
            && a.unfeatheredBitmap == b.unfeatheredBitmap && a.feather == b.feather
            && a.opacity == b.opacity && a.healMode == b.healMode
            && a.placementOrigin == b.placementOrigin
            && a.scaleFactors == b.scaleFactors && a.rotationDegrees == b.rotationDegrees
            && a.selectionRectInBitmap == b.selectionRectInBitmap
            && a.flippedHorizontally == b.flippedHorizontally
            && a.flippedVertically == b.flippedVertically
            && a.surfaceAttached == b.surfaceAttached && a.surfaceQuads == b.surfaceQuads
            && a.hostQuadIndex == b.hostQuadIndex;
    }
    QSizeF displaySize() const;
    QRectF bitmapSelectionRect() const;
    QRectF placementRect() const;
    QTransform bitmapToPlacementTransform() const;
    QVector<QPointF> transformHandlePositions() const;

    QPointF mapPlacementToCanvas(const QPointF &point, int *quadIndex = nullptr) const;
    bool mapCanvasToPlacement(const QPointF &point, QPointF *result,
                              int fallbackQuadIndex = -1) const;
};
