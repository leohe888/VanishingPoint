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
    return edge >= 0 && edge < PerspectiveFacet::CornerCount;
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

// 命中测试：返回点所在的最上层平面索引（后创建的优先），无命中返回 -1
int topmostPlaneIndexAt(const QVector<PerspectivePlane> &planes, const QPointF &point)
{
    for (int i = planes.size() - 1; i >= 0; --i) {
        if (planes[i].containsCanvasPoint(point))
            return i;
    }
    return -1;
}

// 点所属的可绘制面片：平面内用该平面，平面外以整张图像为基准面。
bool resolveFacet(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                  const QPointF &point, PerspectiveFacet *facet)
{
    if (!facet)
        return false;
    const int index = topmostPlaneIndexAt(planes, point);
    if (index >= 0) {
        *facet = planes[index].facet();
        return true;
    }
    if (canvasSize.isEmpty())
        return false;
    // 基准面的展开坐标与画面坐标重合，surfaceToCanvasTransform 因此是恒等的
    const QPointF topLeft(0, 0);
    const QPointF bottomRight(canvasSize.width(), canvasSize.height());
    const PerspectiveFacet::Corners corners{
        topLeft,
        QPointF(bottomRight.x(), topLeft.y()),
        bottomRight,
        QPointF(topLeft.x(), bottomRight.y())
    };
    facet->setCanvasCorners(corners);
    facet->setSurfaceCorners(corners);
    return true;
}

// 保持原单应变换，在展开坐标中平移四个角点后重新投影。
bool translatePlaneOnSurface(const PerspectivePlane &source, const QPointF &dragPoint,
                             const QPointF &pressPoint, PerspectivePlane *result)
{
    if (!result || !qIsFinite(dragPoint.x()) || !qIsFinite(dragPoint.y()))
        return false;
    bool pressOk = false, dragOk = false;
    const QPointF press = source.mapCanvasToSurface(pressPoint, &pressOk);
    const QPointF drag = source.mapCanvasToSurface(dragPoint, &dragOk);
    if (!pressOk || !dragOk)
        return false;
    const PerspectiveTransform projection = source.surfaceToCanvasTransform();
    const QPointF delta = drag - press;
    PerspectivePlane candidate = source;
    for (int i = 0; i < PerspectiveFacet::CornerCount; ++i) {
        const QPointF surfaceCorner = source.surfaceCorners()[i] + delta;
        QPointF canvasCorner;
        if (!projection.mapForward(surfaceCorner, &canvasCorner))
            return false;
        if (!qIsFinite(canvasCorner.x()) || !qIsFinite(canvasCorner.y()) ||
            qAbs(canvasCorner.x()) > 1e7 || qAbs(canvasCorner.y()) > 1e7)
            return false;
        candidate.setSurfaceCorner(i, surfaceCorner);
        candidate.setCanvasCorner(i, canvasCorner);
    }
    if (!candidate.isValid())
        return false;
    *result = candidate;
    return true;
}

// 沿某条边方向缩放平面：只改变该边到对边的距离，保持透视关系不变
PerspectivePlane resizePlaneFromEdge(const PerspectivePlane &source, int edge,
                                     const QPointF &dragPoint, const QPointF &pressPoint)
{
    PerspectivePlane result = source;
    if (!isValidEdgeIndex(edge) ||
        !qIsFinite(dragPoint.x()) || !qIsFinite(dragPoint.y()) ||
        !qIsFinite(pressPoint.x()) || !qIsFinite(pressPoint.y()))
        return result;
    const int next = (edge + 1) % PerspectiveFacet::CornerCount;
    const int oppositeNext = (edge + 2) % PerspectiveFacet::CornerCount;
    const int previous = (edge + 3) % PerspectiveFacet::CornerCount;
    const QPointF a = source.canvasCorners()[edge];
    const QPointF b = source.canvasCorners()[next];
    const QPointF oppositeA = source.canvasCorners()[oppositeNext];
    const QPointF oppositeB = source.canvasCorners()[previous];
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
    const auto aType = QLineF(a, source.canvasCorners()[previous]).intersects(resizedEdge, &movedA);
    const auto bType = QLineF(b, source.canvasCorners()[oppositeNext]).intersects(resizedEdge, &movedB);
    if (aType == QLineF::NoIntersection || bType == QLineF::NoIntersection ||
        !qIsFinite(movedA.x()) || !qIsFinite(movedA.y()) ||
        !qIsFinite(movedB.x()) || !qIsFinite(movedB.y()))
        return result;

    result.setCanvasCorner(edge, movedA);
    result.setCanvasCorner(next, movedB);

    // 展开参数化必须原封不动：用改动前的映射反推新角点的曲面坐标，否则接缝处会错位。
    const PerspectiveTransform projection = source.surfaceToCanvasTransform();
    QPointF surface[2];
    for (int i = 0; i < 2; ++i) {
        const int corner = i == 0 ? edge : next;
        // 新角点落到极点线之外时无法保持展开参数化，此时宁可放弃这次改动。
        if (!projection.mapInverse(result.canvasCorners()[corner], &surface[i]))
            return source;
    }
    result.setSurfaceCorner(edge, surface[0]);
    result.setSurfaceCorner(next, surface[1]);
    return result;
}

// 恢复源平面法线在图像上的投影方向（第三个消失方向），所有垂直平面共享它。
bool projectedNormalDirection(const PerspectivePlane &source, const QPointF &atPoint,
                            const QSize &backgroundSize, QPointF *direction)
{
    // 齐次形式恢复两个消失点，同时覆盖消失点在无穷远的平行线族。
    const Vec3 p0 = imagePoint(source.canvasCorners()[0]);
    const Vec3 p1 = imagePoint(source.canvasCorners()[1]);
    const Vec3 p2 = imagePoint(source.canvasCorners()[2]);
    const Vec3 p3 = imagePoint(source.canvasCorners()[3]);
    const Vec3 line01 = joinLines(p0, p1);
    const Vec3 line32 = joinLines(p3, p2);
    const Vec3 line03 = joinLines(p0, p3);
    const Vec3 line12 = joinLines(p1, p2);
    const Vec3 vanishingX = meetLines(line01, line32);
    const Vec3 vanishingY = meetLines(line03, line12);
    if (dot(vanishingX, vanishingX) < 1e-12 || dot(vanishingY, vanishingY) < 1e-12)
        return false;

    const qreal cx = backgroundSize.width() / 2.0;
    const qreal cy = backgroundSize.height() / 2.0;
    const qreal imageExtent = qMax(backgroundSize.width(), backgroundSize.height());
    qreal focalLength = imageExtent * 1.2;

    // 两个消失点均为有限值时，可由正交性解出焦距。
    if (qAbs(vanishingX.z) > 1e-6 && qAbs(vanishingY.z) > 1e-6) {
        QPointF vx;
        QPointF vy;
        if (toImagePoint(vanishingX, &vx) && toImagePoint(vanishingY, &vy))
            focalLength = focalFromOrthogonalVanishingPoints(vx, vy, backgroundSize);
    }

    const CameraFrame frame{focalLength, cx, cy};
    Vec3 directionX = vanishingDirection(vanishingX, frame);
    Vec3 directionY = vanishingDirection(vanishingY, frame);
    if (!normalize(&directionX) || !normalize(&directionY))
        return false;
    Vec3 normal = cross(directionX, directionY);
    if (!normalize(&normal))
        return false;

    // 3D 法线经内参矩阵投影回图像，即所有垂直平面共享的第三个消失点。
    const Vec3 projected = projectDirection(normal, frame);
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
    result.setSurfaceCorner(0, source.surfaceCorners()[edge]);
    result.setSurfaceCorner(1, source.surfaceCorners()[(edge + 1) % PerspectiveFacet::CornerCount]);
    result.setSurfaceCorner(2, result.surfaceCorners()[1]);
    result.setSurfaceCorner(3, result.surfaceCorners()[0]);
    const QPointF a = source.canvasCorners()[edge];
    const QPointF b = source.canvasCorners()[(edge + 1) % PerspectiveFacet::CornerCount];
    const QPointF midpoint = (a + b) / 2.0;
    QPointF perpendicularAtMidpoint;
    if (!projectedNormalDirection(source, midpoint, backgroundSize, &perpendicularAtMidpoint))
        return result;

    // 指针只控制沿投影后 3D 法线的有符号距离，侧向移动不改变夹角。
    const qreal amount = QPointF::dotProduct(dragPoint - pressPoint,
                                             perpendicularAtMidpoint);
    const QPointF targetMidpoint = midpoint + perpendicularAtMidpoint * amount;
    result.setCanvasCorner(0, a);
    result.setCanvasCorner(1, b);

    if (qAbs(amount) < 2.0) {
        result.setCanvasCorner(2, b);
        result.setCanvasCorner(3, a);
        return result;
    }

    // 共享边与外侧边在 3D 中同向，故交于原边线族的消失点。
    const QPointF oppositeA = source.canvasCorners()[(edge + 2) % PerspectiveFacet::CornerCount];
    const QPointF oppositeB = source.canvasCorners()[(edge + 3) % PerspectiveFacet::CornerCount];
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
                result.setCanvasCorner(2, outerAtB);
                result.setCanvasCorner(3, outerAtA);
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
            result.setCanvasCorner(2, outerAtB);
            result.setCanvasCorner(3, outerAtA);
        } else {
            result.setCanvasCorner(2, b);
            result.setCanvasCorner(3, a);
        }
    }

    // 绕共享边把垂直面展开到曲面上：接缝处曲面坐标相同，外侧边落在源面另一侧。
    const QPointF surfaceA = result.surfaceCorners()[0];
    const QPointF surfaceB = result.surfaceCorners()[1];
    const QPointF surfaceEdge = surfaceB - surfaceA;
    const qreal surfaceEdgeLength = QLineF(surfaceA, surfaceB).length();
    const qreal canvasEdgeLength = qMax(Epsilon, QLineF(a, b).length());
    QPointF outward(-surfaceEdge.y(), surfaceEdge.x());
    const qreal outwardLength = QLineF(QPointF(), outward).length();
    if (outwardLength > Epsilon)
        outward /= outwardLength;
    QPointF sourceCenter;
    for (const QPointF &corner : source.surfaceCorners())
        sourceCenter += corner;
    sourceCenter /= 4.0;
    const QPointF seamCenter = (surfaceA + surfaceB) / 2.0;
    if (QPointF::dotProduct(outward, sourceCenter - seamCenter) > 0.0)
        outward = -outward;
    const qreal canvasDepth =
        (QLineF(result.canvasCorners()[0], result.canvasCorners()[3]).length() +
         QLineF(result.canvasCorners()[1], result.canvasCorners()[2]).length()) / 2.0;
    const qreal surfaceDepth = qMax(1.0, canvasDepth * surfaceEdgeLength / canvasEdgeLength);
    result.setSurfaceCorner(2, surfaceB + outward * surfaceDepth);
    result.setSurfaceCorner(3, surfaceA + outward * surfaceDepth);
    return result;
}

// 把子平面绕共享边做三维旋转，再重新投影回图像。
// 不能用图像平面上的二维圆弧代替：绕三维直线旋转的投影不是圆周运动，
// 那样夹角数值会与几何脱节，调成 0° 时两个平面看上去依然有角度。
PerspectivePlane rotatePlaneAroundEdge(const PerspectivePlane &source, int edge,
                                       qreal targetAngle,
                                       const QSize &backgroundSize)
{
    PerspectivePlane result = source;
    if (!isValidEdgeIndex(edge) || !qIsFinite(targetAngle) || backgroundSize.isEmpty())
        return result;

    const int next = (edge + 1) % PerspectiveFacet::CornerCount;
    const int farB = (edge + 2) % PerspectiveFacet::CornerCount;
    const int farA = (edge + 3) % PerspectiveFacet::CornerCount;
    const QPointF seamA = source.canvasCorners()[edge];
    const QPointF seamB = source.canvasCorners()[next];

    // 1. 子平面的两个消失点：共享边方向 u 与深度方向 v。
    const Vec3 seamLine = joinLines(imagePoint(seamA), imagePoint(seamB));
    const Vec3 outerLine = joinLines(imagePoint(source.canvasCorners()[farA]),
                                     imagePoint(source.canvasCorners()[farB]));
    const Vec3 sideA = joinLines(imagePoint(seamA), imagePoint(source.canvasCorners()[farA]));
    const Vec3 sideB = joinLines(imagePoint(seamB), imagePoint(source.canvasCorners()[farB]));
    const Vec3 vanishingU = meetLines(seamLine, outerLine);
    const Vec3 vanishingV = meetLines(sideA, sideB);
    if (dot(vanishingU, vanishingU) < 1e-12 || dot(vanishingV, vanishingV) < 1e-12)
        return result;

    const qreal cx = backgroundSize.width() / 2.0;
    const qreal cy = backgroundSize.height() / 2.0;
    const qreal imageExtent = qMax(backgroundSize.width(), backgroundSize.height());
    qreal focalLength = imageExtent * 1.2;
    QPointF vuImage;
    QPointF vvImage;
    if (toImagePoint(vanishingU, &vuImage) && toImagePoint(vanishingV, &vvImage))
        focalLength = focalFromOrthogonalVanishingPoints(vuImage, vvImage, backgroundSize);
    const CameraFrame frame{focalLength, cx, cy};

    Vec3 axis = vanishingDirection(vanishingU, frame);
    Vec3 depthDirection = vanishingDirection(vanishingV, frame);
    if (!normalize(&axis) || !normalize(&depthDirection))
        return result;
    Vec3 normal = cross(axis, depthDirection);
    if (!normalize(&normal))
        return result; // 两个消失方向重合，无法定义子平面

    // 2. 把角点反投影到平面 normal·X = h 上；h 只决定整体尺度，由共享边端点射线确定。
    const Vec3 raySeamA = imageRay(seamA, frame);
    const double h = dot(normal, raySeamA);
    if (qAbs(h) < 1e-9)
        return result; // 子平面几乎穿过相机中心
    auto onPlane = [&](const QPointF &p, Vec3 *out) {
        const Vec3 ray = imageRay(p, frame);
        const double denominator = dot(normal, ray);
        if (qAbs(denominator) < 1e-12)
            return false;
        *out = ray * (h / denominator);
        return true;
    };
    Vec3 a;
    Vec3 b;
    Vec3 outerA;
    Vec3 outerB;
    if (!onPlane(seamA, &a) || !onPlane(seamB, &b) ||
        !onPlane(source.canvasCorners()[farA], &outerA) ||
        !onPlane(source.canvasCorners()[farB], &outerB))
        return result;
    // 消失点齐次符号任意：把轴统一成 seamA -> seamB，夹角正方向才不随绕向变化。
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
    auto rotateAroundSeam = [&](const Vec3 &v) {
        // 取 v × axis 为正方向，使 0° 恰好是完全展开、与父平面共面的状态。
        return v * cosine + cross(v, axis) * sine + axis * (dot(axis, v) * (1.0 - cosine));
    };
    const Vec3 movedA = a + rotateAroundSeam(outerA - a);
    const Vec3 movedB = b + rotateAroundSeam(outerB - b);

    // 4. 重新投影回图像。
    QPointF projectedA;
    QPointF projectedB;
    if (!projectPoint(movedA, frame, &projectedA) || !projectPoint(movedB, frame, &projectedB))
        return result;
    result.setCanvasCorner(farA, projectedA);
    result.setCanvasCorner(farB, projectedB);

    // 曲面坐标保持不变：旋转不改变子平面的固有尺寸，纹理应当继续贴合角点。
    qreal normalizedAngle = std::fmod(targetAngle, 360.0);
    if (qFuzzyIsNull(normalizedAngle) && targetAngle > 0.0)
        normalizedAngle = 360.0;
    else if (normalizedAngle < 0.0)
        normalizedAngle += 360.0;
    result.setAngleToParentDegrees(normalizedAngle);
    result.setHasCustomAngle(true);
    if (!result.isValid())
        return source;
    return result;
}
