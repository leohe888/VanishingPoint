#pragma once

#include "perspectivequad.h"

#include <QSize>
#include <QVector>

#include <utility>

class PerspectivePlane
{
public:
    using Corners = PerspectiveQuad::Corners;
    static constexpr int CornerCount = PerspectiveQuad::CornerCount;

    PerspectivePlane() = default;
    PerspectivePlane(Corners canvasCorners, Corners surfaceCorners)
        : m_quad(std::move(canvasCorners), std::move(surfaceCorners)) {}

    const PerspectiveQuad &quad() const { return m_quad; }
    PerspectiveQuad &quad() { return m_quad; }

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
    PerspectiveQuad m_quad;
    int m_surfaceGroupId = -1;      // 平面所属的表面组 ID；默认 -1，表示未分组。
    quint8 m_lockedEdgeMask = 0;    // 四条边的锁定状态位掩码。第 i 位为 1 表示第 i 条边锁定；默认都未锁定。
    int m_parentPlaneIndex = -1;    // 父平面在平面列表中的索引；默认 -1，表示没有父平面。
    int m_parentEdgeIndex = -1;     // 当前平面连接到父平面的边索引；默认 -1。
    qreal m_angleToParentDegrees = 90.0;    // 当前平面相对父平面的夹角，单位为度；默认 90°。
    bool m_hasCustomAngle = false;  // 是否由用户自定义了与父平面的夹角；默认 false。
};

int topmostPlaneIndexAt(const QVector<PerspectivePlane> &planes, const QPointF &point);
bool resolveQuad(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                  const QPointF &point, PerspectiveQuad *quad);

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
