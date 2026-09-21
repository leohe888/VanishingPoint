#include "planeedittool.h"

#include <cmath>
#include <QtMath>

void PlaneEditTool::begin(const PerspectivePlane &plane, const QPointF &pressPoint, int controlPointIndex,
                         int edgeIndex, bool extrude, const QSize &canvasSize,
                         bool rotate, int rotationEdgeIndex)
{
    m_initialPlane = plane;
    m_pressPoint = pressPoint;
    m_controlPointIndex = controlPointIndex;
    m_edgeIndex = edgeIndex;
    m_isExtruding = extrude;
    m_canvasSize = canvasSize;
    m_isRotating = rotate;
    m_rotationEdgeIndex = rotationEdgeIndex;
    if (m_isRotating && rotationEdgeIndex >= 0 &&
        rotationEdgeIndex < PerspectiveFacet::CornerCount) {
        const QPointF seamMidpoint =
            (plane.canvasCorners()[rotationEdgeIndex] +
             plane.canvasCorners()[(rotationEdgeIndex + 1) % PerspectiveFacet::CornerCount]) / 2.0;
        const QPointF pointerFromSeam = pressPoint - seamMidpoint;
        m_lastPointerAngle = std::atan2(pointerFromSeam.y(), pointerFromSeam.x());
        m_accumulatedRotation = 0.0;
    }
}

bool PlaneEditTool::update(const QPointF &point, PerspectivePlane *result)
{
    if (!result)
        return false;
    PerspectivePlane candidate = m_initialPlane;
    if (m_isExtruding) {
        candidate = extrudePerpendicularPlane(
            m_initialPlane, m_edgeIndex, point, m_pressPoint, m_canvasSize);
    } else if (m_isRotating) {
        const int edge = m_rotationEdgeIndex;
        const QPointF seamMidpoint =
            (m_initialPlane.canvasCorners()[edge] +
             m_initialPlane.canvasCorners()[(edge + 1) % PerspectiveFacet::CornerCount]) / 2.0;
        const QPointF pointerFromSeam = point - seamMidpoint;
        if (QLineF(QPointF(), pointerFromSeam).length() < 1e-4)
            return false;
        const qreal currentAngle = std::atan2(pointerFromSeam.y(), pointerFromSeam.x());
        qreal delta = currentAngle - m_lastPointerAngle;
        while (delta > M_PI)
            delta -= 2.0 * M_PI;
        while (delta < -M_PI)
            delta += 2.0 * M_PI;
        m_accumulatedRotation += qRadiansToDegrees(delta);
        m_lastPointerAngle = currentAngle;
        candidate = rotatePlaneAroundEdge(
            m_initialPlane, edge,
            m_initialPlane.angleToParentDegrees() + m_accumulatedRotation, m_canvasSize);
    } else if (m_controlPointIndex >= 0 &&
               m_controlPointIndex < PerspectiveFacet::CornerCount) {
        candidate.setCanvasCorner(m_controlPointIndex, point);
    } else if (m_controlPointIndex >= PerspectiveFacet::CornerCount) {
        candidate = resizePlaneFromEdge(
            m_initialPlane, m_controlPointIndex - PerspectiveFacet::CornerCount,
            point, m_pressPoint);
    } else if (!translatePlaneOnSurface(
                   m_initialPlane, point, m_pressPoint, &candidate)) {
        return false;
    }
    if (!candidate.isValid())
        return false;
    *result = candidate;
    return true;
}
