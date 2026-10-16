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
    // Pick a screen-aligned frame independently of the first click and winding.
    // Only rotate/reverse the perimeter: invalid crossed/concave input stays invalid.
    qreal area = 0;
    for (int i = 0; i < CornerCount; ++i) {
        const QPointF &a = m_points[i], &b = m_points[(i + 1) % CornerCount];
        area += a.x() * b.y() - b.x() * a.y();
    }
    const int step = area < 0 ? -1 : 1;
    PerspectiveQuad::Corners corners;
    qreal bestScore = -3;
    for (int first = 0; first < CornerCount; ++first) {
        PerspectiveQuad::Corners candidate;
        for (int i = 0; i < CornerCount; ++i)
            candidate[i] = m_points[(first + step * i + CornerCount) % CornerCount];
        const QPointF x = candidate[1] - candidate[0] + candidate[2] - candidate[3];
        const QPointF y = candidate[3] - candidate[0] + candidate[2] - candidate[1];
        const qreal score = x.x() / qMax(1e-9, QLineF(QPointF(), x).length())
                          + y.y() / qMax(1e-9, QLineF(QPointF(), y).length());
        const QPointF &a = candidate[0], &b = corners[0];
        const bool earlier = a.x() + a.y() < b.x() + b.y()
            || (a.x() + a.y() == b.x() + b.y() && a.x() < b.x());
        if (score > bestScore + 1e-9 || (qAbs(score - bestScore) <= 1e-9 && earlier)) {
            corners = candidate;
            bestScore = score;
        }
    }
    PerspectivePlane plane;
    plane.quad().setCanvasCorners(corners);

    const qreal surfaceWidth = qMax(1.0,
        (QLineF(plane.quad().canvasCorners()[0], plane.quad().canvasCorners()[1]).length() +        // (上边 + 下边) / 2
         QLineF(plane.quad().canvasCorners()[3], plane.quad().canvasCorners()[2]).length()) / 2.0);
    const qreal surfaceHeight = qMax(1.0,
        (QLineF(plane.quad().canvasCorners()[0], plane.quad().canvasCorners()[3]).length() +        // (左边 + 右边) / 2
         QLineF(plane.quad().canvasCorners()[1], plane.quad().canvasCorners()[2]).length()) / 2.0);
    plane.quad().setSurfaceCorners({QPointF(0, 0), QPointF(surfaceWidth, 0),
                             QPointF(surfaceWidth, surfaceHeight),
                             QPointF(0, surfaceHeight)});
    plane.setSurfaceGroupId(surfaceGroupId);
    return plane;
}
