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
    int m_surfaceGroupId = -1;              // 平面所属的表面组 ID；默认 -1，表示未分组。
    quint8 m_lockedEdgeMask = 0;            // 四条边的锁定状态位掩码。第 i 位为 1 表示第 i 条边锁定；默认都未锁定。
    int m_parentPlaneIndex = -1;            // 父平面在平面列表中的索引；默认 -1，表示没有父平面。
    int m_parentEdgeIndex = -1;             // 当前平面连接到父平面的边索引；默认 -1。
    qreal m_angleToParentDegrees = 90.0;    // 当前平面相对父平面的夹角，单位为度；默认 90°。
    bool m_hasCustomAngle = false;          // 是否由用户自定义了与父平面的夹角；默认 false。
};

/**
 * @brief 查找包含指定画布点的最上层透视平面。
 *
 * 按索引从大到小检查平面，返回首个包含该点的平面索引。
 * 多个平面重叠时，列表中靠后的平面视为更上层。
 *
 * @param planes 按层叠顺序排列的平面列表，索引越大，层级越高。
 * @param point 待检测的点，使用画布坐标。
 * @return 命中平面在 planes 中的索引；列表为空或没有平面包含该点时返回 -1。
 */
int topmostPlaneIndexAt(const QVector<PerspectivePlane> &planes, const QPointF &point);

/**
 * @brief 根据画布点选取用于绘制或取样的四边形。
 *
 * 优先复制包含该点的最上层平面的四边形；没有命中平面时，
 * 构造覆盖整个画布的矩形，其画布坐标与展开坐标相同。
 * 回退到画布矩形时，不要求 point 位于画布范围内。
 *
 * @param planes 待检测的平面列表，列表中靠后的平面优先。
 * @param canvasSize 回退矩形的画布尺寸，仅在没有命中平面时使用。
 * @param point 待检测的点，使用画布坐标。
 * @param[out] quad 接收选取结果的四边形指针，不能为 nullptr。
 * @return 成功写入 quad 时返回 true；输出指针为空，或没有命中平面且
 *         画布尺寸为空时返回 false，失败时不修改输出。
 */
bool resolveQuad(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                  const QPointF &point, PerspectiveQuad *quad);

/**
 * @brief 根据鼠标拖动在展开坐标中平移透视平面。
 *
 * 将按下位置与当前拖动位置映射到源平面的展开坐标，计算位移后，
 * 平移四个展开角点，并通过原有透视变换重新投影到画布。
 * 保留源平面的其他属性，仅在全部角点投影成功且结果有效时写入输出。
 *
 * @param source 要平移的源平面。
 * @param dragPoint 当前鼠标拖动位置，使用画布坐标。
 * @param pressPoint 鼠标按下位置，使用画布坐标。
 * @param[out] result 接收平移结果的平面指针，不能为 nullptr。
 * @return 成功写入 result 时返回 true；输出指针为空、拖动坐标非有限、
 *         坐标映射失败、投影坐标非有限或绝对值超过 1e7，或结果平面
 *         无效时返回 false，失败时不修改输出。
 */
bool translatePlaneOnSurface(const PerspectivePlane &source, const QPointF &dragPoint,
                             const QPointF &pressPoint, PerspectivePlane *result);

/**
 * @brief 根据鼠标拖动调整透视平面的一条边。
 *
 * 对边保持不动，被拖边沿缩放方向移动，并根据原有透视关系计算新的端点。
 * 如果参数无效或无法得到有效结果，则返回未修改的源平面。
 *
 * @param source 要调整的源平面。
 * @param edgeIndex 被拖动边的索引，范围为 [0, PerspectivePlane::CornerCount)。
 * @param dragPoint 当前鼠标位置，使用画布坐标。
 * @param pressPoint 鼠标按下位置，使用画布坐标。
 * @return 调整后的平面；无法调整时返回 source。
 */
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
