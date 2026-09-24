#pragma once

#include "core/perspectiveplane.h"

inline PerspectivePlane makeTestPlane()
{
    PerspectivePlane plane(
        {QPointF(10, 10), QPointF(110, 10), QPointF(110, 80), QPointF(10, 80)},
        {QPointF(0, 0), QPointF(100, 0), QPointF(100, 70), QPointF(0, 70)});
    plane.setSurfaceGroupId(0);
    return plane;
}

inline PerspectivePlane makeSelfIntersectingPlane()
{
    PerspectivePlane plane = makeTestPlane();
    plane.facet().setCanvasCorner(2, QPointF(10, 80));
    plane.facet().setCanvasCorner(3, QPointF(110, 80));
    return plane;
}
