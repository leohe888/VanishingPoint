#pragma once

#include "perspectivefacet.h"

#include <QSize>
#include <QVector>

#include <utility>

class PerspectivePlane
{
public:
    using Corners = PerspectiveFacet::Corners;
    static constexpr int CornerCount = PerspectiveFacet::CornerCount;

    PerspectivePlane() = default;
    PerspectivePlane(Corners canvasCorners, Corners surfaceCorners)
        : m_facet(std::move(canvasCorners), std::move(surfaceCorners)) {}

    const PerspectiveFacet &facet() const { return m_facet; }
    PerspectiveFacet &facet() { return m_facet; }

    int surfaceGroupId() const { return m_surfaceGroupId; }
    void setSurfaceGroupId(int id) { m_surfaceGroupId = id; }

    quint8 lockedEdgeMask() const { return m_lockedEdgeMask; }
    bool isEdgeLocked(int edgeIndex) const;
    void setEdgeLocked(int edgeIndex, bool locked);

    int parentPlaneIndex() const { return m_parentPlaneIndex; }
    int parentEdgeIndex() const { return m_parentEdgeIndex; }
    void setParent(int planeIndex, int edgeIndex);
    void clearParent();

    qreal angleToParentDegrees() const { return m_angleToParentDegrees; }
    void setAngleToParentDegrees(qreal degrees) { m_angleToParentDegrees = degrees; }

    bool hasCustomAngle() const { return m_hasCustomAngle; }
    void setHasCustomAngle(bool adjusted) { m_hasCustomAngle = adjusted; }

private:
    PerspectiveFacet m_facet;
    int m_surfaceGroupId = -1;
    quint8 m_lockedEdgeMask = 0;
    int m_parentPlaneIndex = -1;
    int m_parentEdgeIndex = -1;
    qreal m_angleToParentDegrees = 90.0;
    bool m_hasCustomAngle = false;
};

// 集合查询与编辑算法不属于单个对象，保留为全局函数。
int topmostPlaneIndexAt(const QVector<PerspectivePlane> &planes, const QPointF &point);
bool resolveFacet(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                  const QPointF &point, PerspectiveFacet *facet);

bool translatePlaneOnSurface(const PerspectivePlane &source, const QPointF &dragPoint,
                             const QPointF &pressPoint, PerspectivePlane *result);
PerspectivePlane resizePlaneFromEdge(const PerspectivePlane &source, int edgeIndex,
                                     const QPointF &dragPoint, const QPointF &pressPoint);
bool projectedNormalDirection(const PerspectivePlane &source, const QPointF &atPoint,
                              const QSize &backgroundSize, QPointF *direction);
PerspectivePlane extrudePerpendicularPlane(const PerspectivePlane &source, int edgeIndex,
                                           const QPointF &dragPoint, const QPointF &pressPoint,
                                           const QSize &backgroundSize);
PerspectivePlane rotatePlaneAroundEdge(const PerspectivePlane &source, int edgeIndex,
                                       qreal targetAngleDegrees,
                                       const QSize &backgroundSize);
