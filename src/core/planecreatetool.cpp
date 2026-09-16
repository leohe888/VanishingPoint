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

Plane PlaneCreateTool::makePlane(int surfaceGroup) const
{
    Q_ASSERT(finished());
    Plane plane;
    for (int i = 0; i < CornerCount; ++i)
        plane.corner[i] = m_points[i];

    const qreal surfaceWidth = qMax(1.0,
        (QLineF(plane.corner[0], plane.corner[1]).length() +
         QLineF(plane.corner[3], plane.corner[2]).length()) / 2.0);
    const qreal surfaceHeight = qMax(1.0,
        (QLineF(plane.corner[0], plane.corner[3]).length() +
         QLineF(plane.corner[1], plane.corner[2]).length()) / 2.0);
    plane.surfaceCorner[0] = QPointF(0, 0);
    plane.surfaceCorner[1] = QPointF(surfaceWidth, 0);
    plane.surfaceCorner[2] = QPointF(surfaceWidth, surfaceHeight);
    plane.surfaceCorner[3] = QPointF(0, surfaceHeight);
    plane.surfaceGroup = surfaceGroup;
    return plane;
}
