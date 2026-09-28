#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QVector>

#include <array>

#include "perspectivetransform.h"

class PerspectiveQuad
{
public:
    static constexpr int CornerCount = 4;   // 角点数量
    using Corners = std::array<QPointF, CornerCount>;

    PerspectiveQuad() = default;
    PerspectiveQuad(Corners canvasCorners, Corners surfaceCorners);

    const Corners &canvasCorners() const { return m_canvasCorners; }
    const Corners &surfaceCorners() const { return m_surfaceCorners; }

    void setCanvasCorners(const Corners &corners) { m_canvasCorners = corners; }
    void setSurfaceCorners(const Corners &corners) { m_surfaceCorners = corners; }
    void setCanvasCorner(int index, const QPointF &point);
    void setSurfaceCorner(int index, const QPointF &point);

    QPolygonF canvasPolygon() const;
    QPolygonF surfacePolygon() const;
    // 返回画布上的 8 个控制点：0～3 为角点，4～7 为对应边的中点。
    QVector<QPointF> controlPoints() const;

    bool isValid() const;
    bool containsCanvasPoint(const QPointF &point) const;
    int controlPointIndexAt(const QPointF &point, qreal tolerance) const;
    int edgeIndexAt(const QPointF &point, qreal tolerance) const;

    PerspectiveTransform surfaceToCanvasTransform() const;
    PerspectiveTransform uvToCanvasTransform() const;

private:
    Corners m_canvasCorners{};
    Corners m_surfaceCorners{};
};
