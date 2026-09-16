#pragma once

#include "core/planemath.h"

inline Plane makeTestPlane()
{
    Plane plane;
    plane.corner[0] = QPointF(10, 10);
    plane.corner[1] = QPointF(110, 10);
    plane.corner[2] = QPointF(110, 80);
    plane.corner[3] = QPointF(10, 80);
    plane.surfaceCorner[0] = QPointF(0, 0);
    plane.surfaceCorner[1] = QPointF(100, 0);
    plane.surfaceCorner[2] = QPointF(100, 70);
    plane.surfaceCorner[3] = QPointF(0, 70);
    plane.surfaceGroup = 0;
    plane.name = QStringLiteral("测试平面");
    return plane;
}

inline Plane makeSelfIntersectingPlane()
{
    Plane plane = makeTestPlane();
    plane.corner[2] = QPointF(10, 80);
    plane.corner[3] = QPointF(110, 80);
    return plane;
}
