#include "planecreatetool.h"

#include <QLineF>

void PlaneCreateTool::addPoint(const QPointF &point)
{
    if (!finished())
        m_points.append(point);
}

void PlaneCreateTool::removeLastPoint()
{
    if (!m_points.isEmpty())
        m_points.removeLast();
}

PerspectivePlane PlaneCreateTool::makePlane(int surfaceGroupId) const
{
    Q_ASSERT(finished());
    PerspectivePlane plane;
    for (int i = 0; i < CornerCount; ++i)
        plane.setCanvasCorner(i, m_points[i]);

    const qreal surfaceWidth = qMax(1.0,
        (QLineF(plane.canvasCorners()[0], plane.canvasCorners()[1]).length() +        // (上边 + 下边) / 2
         QLineF(plane.canvasCorners()[3], plane.canvasCorners()[2]).length()) / 2.0);
    const qreal surfaceHeight = qMax(1.0,
        (QLineF(plane.canvasCorners()[0], plane.canvasCorners()[3]).length() +        // (左边 + 右边) / 2
         QLineF(plane.canvasCorners()[1], plane.canvasCorners()[2]).length()) / 2.0);
    plane.setSurfaceCorners({QPointF(0, 0), QPointF(surfaceWidth, 0),
                             QPointF(surfaceWidth, surfaceHeight),
                             QPointF(0, surfaceHeight)});
    plane.setSurfaceGroupId(surfaceGroupId);
    return plane;
}
