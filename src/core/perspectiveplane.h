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

    friend bool operator==(const PerspectivePlane &a, const PerspectivePlane &b)
    {
        return a.m_quad == b.m_quad && a.m_surfaceGroupId == b.m_surfaceGroupId
            && a.m_lockedEdgeMask == b.m_lockedEdgeMask
            && a.m_parentPlaneIndex == b.m_parentPlaneIndex && a.m_parentEdgeIndex == b.m_parentEdgeIndex
            && a.m_angleToParentDegrees == b.m_angleToParentDegrees && a.m_hasCustomAngle == b.m_hasCustomAngle;
    }
    bool controlPointEditable(int handle, bool extrude = false) const;
    bool preservesLockedEdges(const PerspectivePlane &candidate) const;

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
 * 查找包含指定画布点的最上层透视平面。
 *
 * 按索引从大到小检查平面，返回首个包含该点的平面索引。
 * 多个平面重叠时，列表中靠后的平面视为更上层。
 *
 * @return 命中平面在 planes 中的索引；列表为空或没有平面包含该点时返回 -1。
 */
int topmostPlaneIndexAt(const QVector<PerspectivePlane> &planes, const QPointF &point);

/**
 * 根据画布点选取用于绘制或取样的四边形。
 *
 * 优先复制包含该点的最上层平面的四边形；没有命中平面时，
 * 构造覆盖整个画布的矩形，其画布坐标与展开坐标相同。
 * 回退到画布矩形时，不要求 point 位于画布范围内。
 *
 * @return 成功写入 quad 时返回 true；输出指针为空，或没有命中平面且
 *         画布尺寸为空时返回 false，失败时不修改输出。
 */
bool resolveQuad(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                  const QPointF &point, PerspectiveQuad *quad);

/**
 * 根据鼠标拖动在展开坐标中平移透视平面。
 *
 * 将按下位置与当前拖动位置映射到源平面的展开坐标，计算位移后，
 * 平移四个展开角点，并通过原有透视变换重新投影到画布。
 * 保留源平面的其他属性，仅在全部角点投影成功且结果有效时写入输出。
 *
 * @return 成功写入 result 时返回 true；输出指针为空、拖动坐标非有限、
 *         坐标映射失败、投影坐标非有限或绝对值超过 1e7，或结果平面
 *         无效时返回 false，失败时不修改输出。
 */
bool translatePlaneOnSurface(const PerspectivePlane &source, const QPointF &dragPoint,
                             const QPointF &pressPoint, PerspectivePlane *result);

/**
 * 根据鼠标拖动调整透视平面的一条边。
 *
 * 对边保持不动，被拖边沿缩放方向移动，并根据原有透视关系计算新的端点。
 * 如果参数无效或无法得到有效结果，则返回未修改的源平面。
 *
 * @return 调整后的平面；无法调整时返回 source。
 */
PerspectivePlane resizePlaneFromEdge(const PerspectivePlane &source, int edgeIndex,
                                     const QPointF &dragPoint, const QPointF &pressPoint);
/**
 * 计算源平面的三维法线在指定画布点处的二维投影方向。
 *
 * 由两组对边的消失点恢复平面内的三维方向，再通过叉乘得到法线。
 * 假设两组边在空间中正交，相机主点位于背景中心；焦距由消失点估计，
 * 无法得到合理估计时使用背景最长边的 1.2 倍作为回退值。
 * 法线消失点有限时，输出从 atPoint 指向该消失点的单位向量；
 * 消失点位于无穷远时，输出与 atPoint 无关的单位方向。
 *
 * @return 成功写入 direction 时返回 true；消失点或三维方向退化、
 *         投影无法转换为有限点，或最终二维方向长度非有限或过小时返回 false。
 *         失败时不修改输出。
 * @note 返回的是三维法线的透视投影，不保证与画布上的边成直角。
 */
bool projectedNormalDirection(const PerspectivePlane &source, const QPointF &atPoint,
                              const QSize &backgroundSize, QPointF *direction);

/**
 * 根据鼠标拖动，从源平面的一条边构造与之垂直的新平面。
 *
 * 将拖动位移投影到源平面法线的画布方向，确定新平面的伸出距离。
 * 新平面的角点 0、1 对应源边的两个端点，第 0 条边锁定；继承源平面的
 * 表面组 ID，并将展开坐标放到接缝的另一侧，使两面在接缝处连续。
 * 法线方向依赖正交边和估计相机参数，外侧边优先按消失点构造，失败时
 * 尝试平行边方案。展开深度由画布边长比例估计，不是精确的三维长度。
 *
 * @return 新构造的平面；参数无效时返回默认平面，几何计算失败或法线方向的
 *         拖动距离不足 2 个画布坐标单位时，可能返回部分初始化或退化的平面。
 * @note 本函数不做最终有效性检查，调用方须检查返回值的 quad().isValid()。
 *       父平面索引与父边索引由调用方设置；夹角沿用新平面的默认值 90°。
 */
PerspectivePlane extrudePerpendicularPlane(const PerspectivePlane &source, int edgeIndex,
                                           const QPointF &dragPoint, const QPointF &pressPoint,
                                           const QSize &backgroundSize);

/**
 * 将平面绕指定共享边做三维旋转，使其达到目标父子平面夹角。
 *
 * 根据源平面的消失点估计相机及平面姿态，将角点反投影到三维平面，
 * 按 targetAngleDegrees 与 source.angleToParentDegrees() 的差值旋转，
 * 再投影回画布。共享边端点和展开坐标保持不变，仅更新对侧两个画布角点。
 * 成功时记录归一化的目标夹角，并将 hasCustomAngle 设为 true。
 *
 * @return 旋转后的平面；参数无效、几何恢复失败、投影失败或结果四边形
 *         无效时返回未修改的 source。
 * @note 使用背景中心主点和正交消失方向的相机估计；不读取父平面的几何数据。
 *       记录的角度位于 [0, 360]，正的整周角记录为 360°。
 */
PerspectivePlane rotatePlaneAroundEdge(const PerspectivePlane &source, int edgeIndex,
                                       qreal targetAngleDegrees,
                                       const QSize &backgroundSize);
