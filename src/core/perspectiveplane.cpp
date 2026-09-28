#include "perspectiveplane.h"

#include <QLineF>
#include <QtMath>

#include <cmath>

// —— 内部工具：双精度三维向量与相机模型 ——
// 图像坐标可达数千，叉乘中间量超出 float 有效位数，故不用 QVector3D。
namespace {

constexpr qreal Epsilon = 1e-6;

bool isValidEdgeIndex(int edge)
{
    return edge >= 0 && edge < PerspectiveQuad::CornerCount;
}

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 operator+(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3 &a, double s) { return {a.x * s, a.y * s, a.z * s}; }

double dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool normalize(Vec3 *v)
{
    const double length = std::sqrt(dot(*v, *v));
    if (!qIsFinite(length) || length < 1e-12)
        return false;
    *v = *v * (1.0 / length);
    return true;
}

// 图像点 / 图像直线的齐次表示
Vec3 imagePoint(const QPointF &p) { return {p.x(), p.y(), 1.0}; }
Vec3 joinLines(const Vec3 &a, const Vec3 &b) { return cross(a, b); } // 过两点的直线
Vec3 meetLines(const Vec3 &l, const Vec3 &m) { return cross(l, m); } // 两直线的交点

// 齐次点 -> 图像点；位于无穷远（w≈0）时失败
bool toImagePoint(const Vec3 &v, QPointF *out)
{
    if (!out || qAbs(v.z) < 1e-12)
        return false;
    const QPointF p(v.x / v.z, v.y / v.z);
    if (!qIsFinite(p.x()) || !qIsFinite(p.y()))
        return false;
    *out = p;
    return true;
}

// 相机内参的估计值：主点固定在图像中心，焦距由一对正交消失点解出。
struct CameraFrame {
    double focal = 0.0;
    double cx = 0.0;
    double cy = 0.0;
};

// 由一对正交消失点解出焦距；退化时返回与画幅相关的经验值。
double focalFromOrthogonalVanishingPoints(const QPointF &vx, const QPointF &vy,
                                          const QSize &backgroundSize)
{
    const double cx = backgroundSize.width() / 2.0;
    const double cy = backgroundSize.height() / 2.0;
    const double extent = qMax(backgroundSize.width(), backgroundSize.height());
    const double fallback = extent * 1.2;
    if (!qIsFinite(vx.x()) || !qIsFinite(vx.y()) || !qIsFinite(vy.x()) || !qIsFinite(vy.y()))
        return fallback;
    const double squared = -((vx.x() - cx) * (vy.x() - cx) + (vx.y() - cy) * (vy.y() - cy));
    const double minimum = extent * 0.08;
    const double maximum = extent * 20.0;
    if (squared > minimum * minimum && squared < maximum * maximum)
        return std::sqrt(squared);
    return fallback;
}

// 消失点 -> 相机坐标系下的世界方向
Vec3 vanishingDirection(const Vec3 &v, const CameraFrame &frame)
{
    return {v.x - frame.cx * v.z, v.y - frame.cy * v.z, frame.focal * v.z};
}

// 世界方向 -> 图像上的消失点（齐次；w≈0 表示位于无穷远）
Vec3 projectDirection(const Vec3 &d, const CameraFrame &frame)
{
    return {frame.focal * d.x + frame.cx * d.z,
            frame.focal * d.y + frame.cy * d.z,
            d.z};
}

// 图像点 -> 相机坐标系下的射线
Vec3 imageRay(const QPointF &p, const CameraFrame &frame)
{
    return {p.x() - frame.cx, p.y() - frame.cy, frame.focal};
}

// 相机坐标点 -> 图像点（经内参矩阵 K 投影）；落到相机后方或过远都算失败。
bool projectPoint(const Vec3 &p, const CameraFrame &frame, QPointF *out)
{
    if (!out || !(p.z > 1e-9))
        return false;
    const QPointF q(frame.cx + frame.focal * p.x / p.z,
                    frame.cy + frame.focal * p.y / p.z);
    if (!qIsFinite(q.x()) || !qIsFinite(q.y()) ||
        qAbs(q.x()) > 1e7 || qAbs(q.y()) > 1e7)
        return false;
    *out = q;
    return true;
}

// 挤出与旋转共用的平面方向恢复。edgeDirection 沿指定边的消失方向；
// 齐次符号尚未统一，旋转时再按共享边端点确定正方向。
struct PlaneGeometry {
    CameraFrame frame;
    Vec3 edgeDirection;
    Vec3 normal;

    bool pointOnPlane(const QPointF &point, double offset, Vec3 *out) const
    {
        const Vec3 ray = imageRay(point, frame);
        const double denominator = dot(normal, ray);
        if (qAbs(denominator) < 1e-12)
            return false;
        *out = ray * (offset / denominator);
        return true;
    }
};

bool recoverPlaneGeometry(const PerspectivePlane::Corners &corners, int edge,
                          const QSize &backgroundSize, double finitePointThreshold,
                          PlaneGeometry *geometry)
{
    const Vec3 a = imagePoint(corners[edge]);
    const Vec3 b = imagePoint(corners[(edge + 1) % PerspectivePlane::CornerCount]);
    const Vec3 outerB = imagePoint(corners[(edge + 2) % PerspectivePlane::CornerCount]);
    const Vec3 outerA = imagePoint(corners[(edge + 3) % PerspectivePlane::CornerCount]);
    const Vec3 vanishingEdge = meetLines(joinLines(a, b), joinLines(outerA, outerB));
    const Vec3 vanishingDepth = meetLines(joinLines(a, outerA), joinLines(b, outerB));
    if (dot(vanishingEdge, vanishingEdge) < 1e-12 ||
        dot(vanishingDepth, vanishingDepth) < 1e-12)
        return false;

    double focal = qMax(backgroundSize.width(), backgroundSize.height()) * 1.2;
    QPointF edgePoint;
    QPointF depthPoint;
    if (qAbs(vanishingEdge.z) > finitePointThreshold &&
        qAbs(vanishingDepth.z) > finitePointThreshold &&
        toImagePoint(vanishingEdge, &edgePoint) && toImagePoint(vanishingDepth, &depthPoint))
        focal = focalFromOrthogonalVanishingPoints(edgePoint, depthPoint, backgroundSize);
    geometry->frame = {focal, backgroundSize.width() / 2.0, backgroundSize.height() / 2.0};

    geometry->edgeDirection = vanishingDirection(vanishingEdge, geometry->frame);
    Vec3 depthDirection = vanishingDirection(vanishingDepth, geometry->frame);
    if (!normalize(&geometry->edgeDirection) || !normalize(&depthDirection))
        return false;
    geometry->normal = cross(geometry->edgeDirection, depthDirection);
    return normalize(&geometry->normal);
}

Vec3 rotateVectorAroundAxis(const Vec3 &v, const Vec3 &axis, double cosine, double sine)
{
    // 完整 Rodrigues 公式；取 v × axis 为夹角增加的方向。
    return v * cosine + cross(v, axis) * sine + axis * (dot(axis, v) * (1.0 - cosine));
}

} // namespace

bool PerspectivePlane::isEdgeLocked(int edgeIndex) const
{
    return edgeIndex >= 0 && edgeIndex < CornerCount &&
           (m_lockedEdgeMask & quint8(1u << edgeIndex));
}

void PerspectivePlane::setEdgeLocked(int edgeIndex, bool locked)
{
    if (edgeIndex < 0 || edgeIndex >= CornerCount)
        return;
    const quint8 edgeBit = quint8(1u << edgeIndex);
    if (locked)
        m_lockedEdgeMask |= edgeBit;
    else
        m_lockedEdgeMask &= quint8(~edgeBit);
}

void PerspectivePlane::setParent(int planeIndex, int edgeIndex)
{
    m_parentPlaneIndex = planeIndex;
    m_parentEdgeIndex = edgeIndex;
}

void PerspectivePlane::clearParent()
{
    m_parentPlaneIndex = -1;
    m_parentEdgeIndex = -1;
    m_angleToParentDegrees = 90.0;
    m_hasCustomAngle = false;
}

// 查找包含指定点的最上层平面的索引，没有时返回 -1
int topmostPlaneIndexAt(const QVector<PerspectivePlane> &planes, const QPointF &point)
{
    for (int i = planes.size() - 1; i >= 0; --i) {
        if (planes[i].quad().containsCanvasPoint(point))
            return i;
    }
    return -1;
}

// 根据画布点 point，选出后续绘制或取样要用的四边形，并写入 quad
bool resolveQuad(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                  const QPointF &point, PerspectiveQuad *quad)
{
    // 如果输出指针 quad 是空指针，返回 false
    if (!quad)
        return false;

    // 查找包含指定点的最上层平面的索引。找到时复制该平面的四边形到 quad 并返回 true。因此多个平面重叠时，列表中靠后的平面优先。
    const int index = topmostPlaneIndexAt(planes, point);
    if (index >= 0) {
        *quad = planes[index].quad();
        return true;
    }

    // 如果没有平面包含该点，就用 canvasSize 构造一个覆盖整张画布的矩形四边形，并将它写入 quad。画布尺寸为空时无法构造，返回 false
    if (canvasSize.isEmpty())
        return false;
    const QPointF topLeft(0, 0);
    const QPointF bottomRight(canvasSize.width(), canvasSize.height());
    const PerspectiveQuad::Corners corners{
        topLeft,
        QPointF(bottomRight.x(), topLeft.y()),
        bottomRight,
        QPointF(topLeft.x(), bottomRight.y())
    };
    quad->setCanvasCorners(corners);
    quad->setSurfaceCorners(corners);
    return true;
}

// 保持原单应变换，在展开坐标中平移四个角点后重新投影。
bool translatePlaneOnSurface(const PerspectivePlane &source, const QPointF &dragPoint,
                             const QPointF &pressPoint, PerspectivePlane *result)
{
    if (!result || !qIsFinite(dragPoint.x()) || !qIsFinite(dragPoint.y()))
        return false;

    // 把拖动起点和当前点从画布坐标映射到平面的展开坐标
    bool pressOk = false, dragOk = false;
    const QPointF press = source.quad().mapCanvasToSurface(pressPoint, &pressOk);
    const QPointF drag = source.quad().mapCanvasToSurface(dragPoint, &dragOk);
    if (!pressOk || !dragOk)
        return false;

    // 计算两点在展开坐标中的位移 delta
    const QPointF delta = drag - press;

    const PerspectiveTransform projection = source.quad().surfaceToCanvasTransform();
    PerspectivePlane candidate = source;
    // 将四个角的展开坐标都平移 delta，再用原来的透视变换投影回画布坐标。两组角点一起更新，变换关系保持不变
    for (int i = 0; i < PerspectiveQuad::CornerCount; ++i) {
        const QPointF surfaceCorner = source.quad().surfaceCorners()[i] + delta;
        QPointF canvasCorner;
        if (!projection.mapForward(surfaceCorner, &canvasCorner))
            return false;
        if (!qIsFinite(canvasCorner.x()) || !qIsFinite(canvasCorner.y()) ||
            qAbs(canvasCorner.x()) > 1e7 || qAbs(canvasCorner.y()) > 1e7)
            return false;
        candidate.quad().setSurfaceCorner(i, surfaceCorner);
        candidate.quad().setCanvasCorner(i, canvasCorner);
    }
    if (!candidate.quad().isValid())
        return false;
    *result = candidate;
    return true;
}

// 沿某条边方向缩放平面：只改变该边到对边的距离，保持透视关系不变
PerspectivePlane resizePlaneFromEdge(const PerspectivePlane &source, int edge,
                                     const QPointF &dragPoint, const QPointF &pressPoint)
{
    // 先保存原平面并检查输入
    PerspectivePlane result = source;
    if (!isValidEdgeIndex(edge) ||
        !qIsFinite(dragPoint.x()) || !qIsFinite(dragPoint.y()) ||
        !qIsFinite(pressPoint.x()) || !qIsFinite(pressPoint.y()))
        return result;

    const int next = (edge + 1) % PerspectiveQuad::CornerCount;
    const int oppositeNext = (edge + 2) % PerspectiveQuad::CornerCount;
    const int previous = (edge + 3) % PerspectiveQuad::CornerCount;

    // a、b 是被拖边的两个角，oppositeA、oppositeB 是对边的两个角。edgeMidpoint 和 oppositeMidpoint 分别是两条边的中点
    const QPointF a = source.quad().canvasCorners()[edge];
    const QPointF b = source.quad().canvasCorners()[next];
    const QPointF oppositeA = source.quad().canvasCorners()[oppositeNext];
    const QPointF oppositeB = source.quad().canvasCorners()[previous];
    const QPointF edgeMidpoint = (a + b) / 2.0;
    const QPointF oppositeMidpoint = (oppositeA + oppositeB) / 2.0;

    // 沿边缩放只有一个自由度：忽略指针侧向移动，只取沿延伸轴的位移。
    QPointF extensionAxis = edgeMidpoint - oppositeMidpoint;
    qreal axisLength = QLineF(QPointF(), extensionAxis).length();
    if (axisLength < Epsilon) {
        extensionAxis = QPointF(-(b - a).y(), (b - a).x());
        axisLength = QLineF(QPointF(), extensionAxis).length();
    }
    if (axisLength < Epsilon)
        return result;
    extensionAxis /= axisLength;
    const qreal extension = QPointF::dotProduct(dragPoint - pressPoint, extensionAxis);
    const QPointF targetPoint = edgeMidpoint + extensionAxis * extension;

    // 缩放后的边必须保留原边方向的消失点。
    QPointF edgeVanishingPoint;
    const auto vpType = QLineF(a, b).intersects(QLineF(oppositeA, oppositeB),
                                                &edgeVanishingPoint);
    QLineF resizedEdge;
    if (vpType != QLineF::NoIntersection && qIsFinite(edgeVanishingPoint.x()) &&
        qIsFinite(edgeVanishingPoint.y()) &&
        QLineF(edgeVanishingPoint, targetPoint).length() > 1.0 &&
        QLineF(edgeVanishingPoint, edgeMidpoint).length() < 1e7) {
        resizedEdge = QLineF(edgeVanishingPoint, targetPoint);
    } else {
        // 对边平行是消失点位于无穷远处的极限情形。
        resizedEdge = QLineF(targetPoint, targetPoint + (b - a));
    }

    // 端点约束在各自原侧边线上，垂直平面缩放时才只改高度、保持垂直。
    QPointF movedA;
    QPointF movedB;
    const auto aType = QLineF(a, source.quad().canvasCorners()[previous]).intersects(resizedEdge, &movedA);
    const auto bType = QLineF(b, source.quad().canvasCorners()[oppositeNext]).intersects(resizedEdge, &movedB);
    if (aType == QLineF::NoIntersection || bType == QLineF::NoIntersection ||
        !qIsFinite(movedA.x()) || !qIsFinite(movedA.y()) ||
        !qIsFinite(movedB.x()) || !qIsFinite(movedB.y()))
        return result;

    result.quad().setCanvasCorner(edge, movedA);
    result.quad().setCanvasCorner(next, movedB);

    // 展开参数化必须原封不动：用改动前的映射反推新角点的曲面坐标，否则接缝处会错位。
    const PerspectiveTransform projection = source.quad().surfaceToCanvasTransform();
    QPointF surface[2];
    for (int i = 0; i < 2; ++i) {
        const int corner = i == 0 ? edge : next;
        // 新角点落到极点线之外时无法保持展开参数化，此时宁可放弃这次改动。
        if (!projection.mapInverse(result.quad().canvasCorners()[corner], &surface[i]))
            return source;
    }
    result.quad().setSurfaceCorner(edge, surface[0]);
    result.quad().setSurfaceCorner(next, surface[1]);
    return result;
}

// 恢复源平面法线在图像上的投影方向（第三个消失方向），所有垂直平面共享它。
bool projectedNormalDirection(const PerspectivePlane &source, const QPointF &atPoint,
                            const QSize &backgroundSize, QPointF *direction)
{
    PlaneGeometry geometry;
    // 保留法线恢复原有的有限消失点阈值；旋转使用更小的阈值。
    if (!recoverPlaneGeometry(source.quad().canvasCorners(), 0, backgroundSize, 1e-6, &geometry))
        return false;

    // 3D 法线经内参矩阵投影回图像，即所有垂直平面共享的第三个消失点。
    const Vec3 projected = projectDirection(geometry.normal, geometry.frame);
    QPointF projectedDirection;
    if (qAbs(projected.z) > 1e-6) {
        // 齐次分量 w 非零：第三个消失点是有限点。
        QPointF perpendicularVanishingPoint;
        if (!toImagePoint(projected, &perpendicularVanishingPoint))
            return false;
        projectedDirection = perpendicularVanishingPoint - atPoint;
    } else {
        // 齐次分量 w 为零意味着第三个消失点位于无穷远处。
        projectedDirection = QPointF(projected.x, projected.y);
    }

    const qreal length = QLineF(QPointF(), projectedDirection).length();
    if (!qIsFinite(length) || length < Epsilon)
        return false;
    *direction = projectedDirection / length;
    return true;
}

// 从源平面的一条边拖出与之垂直的新平面（Ctrl+拖动边缘）
PerspectivePlane extrudePerpendicularPlane(const PerspectivePlane &source, int edge,
                                           const QPointF &dragPoint,
                                           const QPointF &pressPoint,
                                           const QSize &backgroundSize)
{
    PerspectivePlane result;
    if (!isValidEdgeIndex(edge) || backgroundSize.isEmpty() ||
        !qIsFinite(dragPoint.x()) || !qIsFinite(dragPoint.y()) ||
        !qIsFinite(pressPoint.x()) || !qIsFinite(pressPoint.y()))
        return result;
    result.setSurfaceGroupId(source.surfaceGroupId());
    result.setEdgeLocked(0, true); // 新平面的第 0 条边就是与源平面共用的边
    result.quad().setSurfaceCorner(0, source.quad().surfaceCorners()[edge]);
    result.quad().setSurfaceCorner(1, source.quad().surfaceCorners()[(edge + 1) % PerspectiveQuad::CornerCount]);
    result.quad().setSurfaceCorner(2, result.quad().surfaceCorners()[1]);
    result.quad().setSurfaceCorner(3, result.quad().surfaceCorners()[0]);
    const QPointF a = source.quad().canvasCorners()[edge];
    const QPointF b = source.quad().canvasCorners()[(edge + 1) % PerspectiveQuad::CornerCount];
    const QPointF midpoint = (a + b) / 2.0;
    QPointF perpendicularAtMidpoint;
    if (!projectedNormalDirection(source, midpoint, backgroundSize, &perpendicularAtMidpoint))
        return result;

    // 指针只控制沿投影后 3D 法线的有符号距离，侧向移动不改变夹角。
    const qreal amount = QPointF::dotProduct(dragPoint - pressPoint,
                                             perpendicularAtMidpoint);
    const QPointF targetMidpoint = midpoint + perpendicularAtMidpoint * amount;
    result.quad().setCanvasCorner(0, a);
    result.quad().setCanvasCorner(1, b);

    if (qAbs(amount) < 2.0) {
        result.quad().setCanvasCorner(2, b);
        result.quad().setCanvasCorner(3, a);
        return result;
    }

    // 共享边与外侧边在 3D 中同向，故交于原边线族的消失点。
    const QPointF oppositeA = source.quad().canvasCorners()[(edge + 2) % PerspectiveQuad::CornerCount];
    const QPointF oppositeB = source.quad().canvasCorners()[(edge + 3) % PerspectiveQuad::CornerCount];
    QPointF edgeVanishingPoint;
    const QLineF::IntersectionType vpType =
        QLineF(a, b).intersects(QLineF(oppositeA, oppositeB), &edgeVanishingPoint);

    bool constructedWithVanishingPoint = false;
    if (vpType != QLineF::NoIntersection && qIsFinite(edgeVanishingPoint.x()) &&
        qIsFinite(edgeVanishingPoint.y()) &&
        QLineF(edgeVanishingPoint, (a + b) / 2.0).length() < 1e7) {
        if (QLineF(edgeVanishingPoint, targetMidpoint).length() > 1.0) {
            QPointF outerAtB;
            QPointF outerAtA;
            const QLineF outerLine(edgeVanishingPoint, targetMidpoint);
            QPointF perpendicularAtA;
            QPointF perpendicularAtB;
            if (!projectedNormalDirection(source, a, backgroundSize, &perpendicularAtA) ||
                !projectedNormalDirection(source, b, backgroundSize, &perpendicularAtB))
                return result;
            const auto bType = QLineF(b, b + perpendicularAtB).intersects(outerLine, &outerAtB);
            const auto aType = QLineF(a, a + perpendicularAtA).intersects(outerLine, &outerAtA);
            if (bType != QLineF::NoIntersection && aType != QLineF::NoIntersection &&
                qIsFinite(outerAtA.x()) && qIsFinite(outerAtA.y()) &&
                qIsFinite(outerAtB.x()) && qIsFinite(outerAtB.y())) {
                result.quad().setCanvasCorner(2, outerAtB);
                result.quad().setCanvasCorner(3, outerAtA);
                constructedWithVanishingPoint = true;
            }
        }
    }

    // 对边平行时消失点在无穷远：外侧边保持平行，端点仍沿垂直方向移动。
    if (!constructedWithVanishingPoint) {
        const QLineF outerLine(targetMidpoint, targetMidpoint + (b - a));
        QPointF perpendicularAtA;
        QPointF perpendicularAtB;
        QPointF outerAtA;
        QPointF outerAtB;
        if (projectedNormalDirection(source, a, backgroundSize, &perpendicularAtA) &&
            projectedNormalDirection(source, b, backgroundSize, &perpendicularAtB) &&
            QLineF(a, a + perpendicularAtA).intersects(outerLine, &outerAtA) !=
                QLineF::NoIntersection &&
            QLineF(b, b + perpendicularAtB).intersects(outerLine, &outerAtB) !=
                QLineF::NoIntersection) {
            result.quad().setCanvasCorner(2, outerAtB);
            result.quad().setCanvasCorner(3, outerAtA);
        } else {
            result.quad().setCanvasCorner(2, b);
            result.quad().setCanvasCorner(3, a);
        }
    }

    // 绕共享边把垂直面展开到曲面上：接缝处曲面坐标相同，外侧边落在源面另一侧。
    const QPointF surfaceA = result.quad().surfaceCorners()[0];
    const QPointF surfaceB = result.quad().surfaceCorners()[1];
    const QPointF surfaceEdge = surfaceB - surfaceA;
    const qreal surfaceEdgeLength = QLineF(surfaceA, surfaceB).length();
    const qreal canvasEdgeLength = qMax(Epsilon, QLineF(a, b).length());
    QPointF outward(-surfaceEdge.y(), surfaceEdge.x());
    const qreal outwardLength = QLineF(QPointF(), outward).length();
    if (outwardLength > Epsilon)
        outward /= outwardLength;
    QPointF sourceCenter;
    for (const QPointF &corner : source.quad().surfaceCorners())
        sourceCenter += corner;
    sourceCenter /= 4.0;
    const QPointF seamCenter = (surfaceA + surfaceB) / 2.0;
    if (QPointF::dotProduct(outward, sourceCenter - seamCenter) > 0.0)
        outward = -outward;
    const qreal canvasDepth =
        (QLineF(result.quad().canvasCorners()[0], result.quad().canvasCorners()[3]).length() +
         QLineF(result.quad().canvasCorners()[1], result.quad().canvasCorners()[2]).length()) / 2.0;
    const qreal surfaceDepth = qMax(1.0, canvasDepth * surfaceEdgeLength / canvasEdgeLength);
    result.quad().setSurfaceCorner(2, surfaceB + outward * surfaceDepth);
    result.quad().setSurfaceCorner(3, surfaceA + outward * surfaceDepth);
    return result;
}

// 按目标夹角与当前记录夹角之差，绕共享边做三维旋转，再投影回图像。
PerspectivePlane rotatePlaneAroundEdge(const PerspectivePlane &source, int edge,
                                       qreal targetAngle,
                                       const QSize &backgroundSize)
{
    PerspectivePlane result = source;
    if (!isValidEdgeIndex(edge) || !qIsFinite(targetAngle) || backgroundSize.isEmpty())
        return result;

    const int next = (edge + 1) % PerspectiveQuad::CornerCount;
    const int farB = (edge + 2) % PerspectiveQuad::CornerCount;
    const int farA = (edge + 3) % PerspectiveQuad::CornerCount;
    const QPointF seamA = source.quad().canvasCorners()[edge];
    const QPointF seamB = source.quad().canvasCorners()[next];

    // 1. 恢复平面方向与投影参数；有限消失点沿用 toImagePoint 的判定。
    PlaneGeometry geometry;
    if (!recoverPlaneGeometry(source.quad().canvasCorners(), edge, backgroundSize, 0.0, &geometry))
        return result;

    // 2. 把角点反投影到平面 normal·X = h 上；h 只决定整体尺度，由共享边端点射线确定。
    const double h = dot(geometry.normal, imageRay(seamA, geometry.frame));
    if (qAbs(h) < 1e-9)
        return result; // 子平面几乎穿过相机中心
    Vec3 a;
    Vec3 b;
    Vec3 outerA;
    Vec3 outerB;
    if (!geometry.pointOnPlane(seamA, h, &a) || !geometry.pointOnPlane(seamB, h, &b) ||
        !geometry.pointOnPlane(source.quad().canvasCorners()[farA], h, &outerA) ||
        !geometry.pointOnPlane(source.quad().canvasCorners()[farB], h, &outerB))
        return result;
    // 消失点齐次符号任意：把轴统一成 seamA -> seamB，夹角正方向才不随绕向变化。
    Vec3 axis = geometry.edgeDirection;
    if (dot(b - a, axis) < 0.0)
        axis = axis * -1.0;

    // 3. 绕共享边旋转 Δ；用完整 Rodrigues 公式，子平面被拖成一般四边形也能保持刚体旋转。
    qreal delta = targetAngle - source.angleToParentDegrees();
    while (delta > 180.0)
        delta -= 360.0;
    while (delta <= -180.0)
        delta += 360.0;
    const qreal radians = qDegreesToRadians(delta);
    const double cosine = qCos(radians);
    const double sine = qSin(radians);
    const Vec3 movedA = a + rotateVectorAroundAxis(outerA - a, axis, cosine, sine);
    const Vec3 movedB = b + rotateVectorAroundAxis(outerB - b, axis, cosine, sine);

    // 4. 重新投影回图像。
    QPointF projectedA;
    QPointF projectedB;
    if (!projectPoint(movedA, geometry.frame, &projectedA) ||
        !projectPoint(movedB, geometry.frame, &projectedB))
        return result;
    result.quad().setCanvasCorner(farA, projectedA);
    result.quad().setCanvasCorner(farB, projectedB);

    // 曲面坐标保持不变：旋转不改变子平面的固有尺寸，纹理应当继续贴合角点。
    qreal normalizedAngle = std::fmod(targetAngle, 360.0);
    if (qFuzzyIsNull(normalizedAngle) && targetAngle > 0.0)
        normalizedAngle = 360.0;
    else if (normalizedAngle < 0.0)
        normalizedAngle += 360.0;
    result.setAngleToParentDegrees(normalizedAngle);
    result.setHasCustomAngle(true);
    if (!result.quad().isValid())
        return source;
    return result;
}
