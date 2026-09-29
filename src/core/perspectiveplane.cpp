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

int topmostPlaneIndexAt(const QVector<PerspectivePlane> &planes, const QPointF &point)
{
    // 从列表末尾向前遍历，优先检查层级更高的平面。
    for (int i = planes.size() - 1; i >= 0; --i) {
        // 检查当前平面的四边形是否包含画布点；首次命中即为最上层平面，返回其索引。
        if (planes[i].quad().containsCanvasPoint(point))
            return i;
    }
    // 列表为空或所有平面均未命中时，返回 -1 表示没有找到平面。
    return -1;
}

bool resolveQuad(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                  const QPointF &point, PerspectiveQuad *quad)
{
    // 检查输出指针，避免向空指针写入结果。
    if (!quad)
        return false;

    // 查找包含画布点的最上层平面；命中时复制其四边形并立即返回成功。
    const int index = topmostPlaneIndexAt(planes, point);
    if (index >= 0) {
        *quad = planes[index].quad();
        return true;
    }

    // 没有命中平面时检查画布尺寸；尺寸为空则无法构造回退矩形。
    if (canvasSize.isEmpty())
        return false;
    // 按左上、右上、右下、左下的顺序构造覆盖整个画布的四个角点。
    const QPointF topLeft(0, 0);
    const QPointF bottomRight(canvasSize.width(), canvasSize.height());
    const PerspectiveQuad::Corners corners{
        topLeft,
        QPointF(bottomRight.x(), topLeft.y()),
        bottomRight,
        QPointF(topLeft.x(), bottomRight.y())
    };
    // 将矩形同时用作画布角点和展开角点，写入回退结果并返回成功。
    quad->setCanvasCorners(corners);
    quad->setSurfaceCorners(corners);
    return true;
}

bool translatePlaneOnSurface(const PerspectivePlane &source, const QPointF &dragPoint,
                             const QPointF &pressPoint, PerspectivePlane *result)
{
    // 检查输出指针和当前拖动坐标，拒绝空指针及非有限坐标。
    if (!result || !qIsFinite(dragPoint.x()) || !qIsFinite(dragPoint.y()))
        return false;

    // 将按下位置和当前拖动位置映射到展开坐标；任一映射失败则终止。
    const PerspectiveTransform projection = source.quad().surfaceToCanvasTransform();
    QPointF press;
    QPointF drag;
    if (!projection.mapInverse(pressPoint, &press) || !projection.mapInverse(dragPoint, &drag))
        return false;

    // 计算展开坐标中的位移，使四个角点沿平面移动相同距离。
    const QPointF delta = drag - press;

    // 复制源平面作为候选结果，复用同一变换投影回画布。
    PerspectivePlane candidate = source;
    // 平移每个展开角点，再用原变换投影回画布；两组角点一起更新。
    for (int i = 0; i < PerspectiveQuad::CornerCount; ++i) {
        const QPointF surfaceCorner = source.quad().surfaceCorners()[i] + delta;
        QPointF canvasCorner;
        // 投影失败或画布坐标非有限、绝对值超过 1e7 时终止，避免写入异常结果。
        if (!projection.mapForward(surfaceCorner, &canvasCorner))
            return false;
        if (!qIsFinite(canvasCorner.x()) || !qIsFinite(canvasCorner.y()) ||
            qAbs(canvasCorner.x()) > 1e7 || qAbs(canvasCorner.y()) > 1e7)
            return false;
        candidate.quad().setSurfaceCorner(i, surfaceCorner);
        candidate.quad().setCanvasCorner(i, canvasCorner);
    }
    // 检查候选四边形是否有效；全部检查通过后才写入输出，失败时保留原输出。
    if (!candidate.quad().isValid())
        return false;
    *result = candidate;
    return true;
}

PerspectivePlane resizePlaneFromEdge(const PerspectivePlane &source, int edge,
                                     const QPointF &dragPoint, const QPointF &pressPoint)
{
    PerspectivePlane result = source;
    if (!isValidEdgeIndex(edge) ||
        !qIsFinite(dragPoint.x()) || !qIsFinite(dragPoint.y()) ||
        !qIsFinite(pressPoint.x()) || !qIsFinite(pressPoint.y()))
        return result;

    // a 和 b 是被拖边的端点；oppositeA 和 oppositeB 是对边的端点；edgeMidpoint 和 oppositeMidpoint 分别是被拖边和对边的中点。
    const int next = (edge + 1) % PerspectiveQuad::CornerCount;
    const int oppositeNext = (edge + 2) % PerspectiveQuad::CornerCount;
    const int previous = (edge + 3) % PerspectiveQuad::CornerCount;
    const QPointF a = source.quad().canvasCorners()[edge];
    const QPointF b = source.quad().canvasCorners()[next];
    const QPointF oppositeA = source.quad().canvasCorners()[oppositeNext];
    const QPointF oppositeB = source.quad().canvasCorners()[previous];
    const QPointF edgeMidpoint = (a + b) / 2.0;
    const QPointF oppositeMidpoint = (oppositeA + oppositeB) / 2.0;

    // 缩放只沿对边中点到被拖边中点的方向进行，忽略鼠标沿边方向的移动。
    QPointF extensionAxis = edgeMidpoint - oppositeMidpoint;
    qreal axisLength = QLineF(QPointF(), extensionAxis).length();
    if (axisLength < Epsilon) {
        extensionAxis = QPointF(-(b - a).y(), (b - a).x());
        axisLength = QLineF(QPointF(), extensionAxis).length();
    }
    if (axisLength < Epsilon)
        return result;
    extensionAxis /= axisLength;
    // 把鼠标位移投影到缩放方向，得到被拖边中点的移动距离和目标位置。
    const qreal extension = QPointF::dotProduct(dragPoint - pressPoint, extensionAxis);
    const QPointF targetPoint = edgeMidpoint + extensionAxis * extension;

    // 新边经过目标中点，并保留原边的消失点方向；平行边没有有限消失点，单独处理。
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
        // 两组边平行时消失点在无穷远处，直接沿旧边方向构造新边。
        resizedEdge = QLineF(targetPoint, targetPoint + (b - a));
    }

    // 新边与原来的两条侧边延长线相交，交点就是被拖边的新端点；对边因此保持不动。
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

    // 用原透视映射反算新端点的 surface 坐标，保持平面的展开坐标系和共享边对应关系。
    const PerspectiveTransform projection = source.quad().surfaceToCanvasTransform();
    QPointF surface[2];
    for (int i = 0; i < 2; ++i) {
        const int corner = i == 0 ? edge : next;
        // 新端点无法通过原映射反算时，放弃本次调整并返回源平面。
        if (!projection.mapInverse(result.quad().canvasCorners()[corner], &surface[i]))
            return source;
    }
    result.quad().setSurfaceCorner(edge, surface[0]);
    result.quad().setSurfaceCorner(next, surface[1]);
    return result;
}

bool projectedNormalDirection(const PerspectivePlane &source, const QPointF &atPoint,
                            const QSize &backgroundSize, QPointF *direction)
{
    // 将角点转为齐次坐标，通过叉乘求直线，再求两组对边的消失点。
    // 保留齐次形式，使平行对边产生的无穷远消失点也能参与后续计算。
    const Vec3 p0 = imagePoint(source.quad().canvasCorners()[0]);
    const Vec3 p1 = imagePoint(source.quad().canvasCorners()[1]);
    const Vec3 p2 = imagePoint(source.quad().canvasCorners()[2]);
    const Vec3 p3 = imagePoint(source.quad().canvasCorners()[3]);
    const Vec3 line01 = joinLines(p0, p1);
    const Vec3 line32 = joinLines(p3, p2);
    const Vec3 line03 = joinLines(p0, p3);
    const Vec3 line12 = joinLines(p1, p2);
    const Vec3 vanishingX = meetLines(line01, line32);
    const Vec3 vanishingY = meetLines(line03, line12);
    // 接近零向量的齐次交点无法表示有效消失点（例如两条边线重合）。
    if (dot(vanishingX, vanishingX) < 1e-12 || dot(vanishingY, vanishingY) < 1e-12)
        return false;

    // 以背景中心为主点，先设置经验焦距，再尝试由正交消失点估计焦距。
    const qreal cx = backgroundSize.width() / 2.0;
    const qreal cy = backgroundSize.height() / 2.0;
    const qreal imageExtent = qMax(backgroundSize.width(), backgroundSize.height());
    qreal focalLength = imageExtent * 1.2;

    // 两个消失点均有限时，利用 f² = -(vx - c)·(vy - c) 解出焦距。
    // 辅助函数会在估计不合理时回退到经验值；无穷远消失点则直接使用经验值。
    if (qAbs(vanishingX.z) > 1e-6 && qAbs(vanishingY.z) > 1e-6) {
        QPointF vx;
        QPointF vy;
        if (toImagePoint(vanishingX, &vx) && toImagePoint(vanishingY, &vy))
            focalLength = focalFromOrthogonalVanishingPoints(vx, vy, backgroundSize);
    }

    // 消失点经相机内参的逆映射恢复三维方向，归一化后叉乘求法线。
    // 两方向退化或近乎平行时无法得到有效法线。
    const CameraFrame frame{focalLength, cx, cy};
    Vec3 directionX = vanishingDirection(vanishingX, frame);
    Vec3 directionY = vanishingDirection(vanishingY, frame);
    if (!normalize(&directionX) || !normalize(&directionY))
        return false;
    Vec3 normal = cross(directionX, directionY);
    if (!normalize(&normal))
        return false;

    // 将三维法线投影为第三个消失点，确定 atPoint 处的二维方向。
    const Vec3 projected = projectDirection(normal, frame);
    QPointF projectedDirection;
    if (qAbs(projected.z) > 1e-6) {
        // 齐次分量 w 足够大：转为有限消失点，方向从 atPoint 指向它。
        QPointF perpendicularVanishingPoint;
        if (!toImagePoint(projected, &perpendicularVanishingPoint))
            return false;
        projectedDirection = perpendicularVanishingPoint - atPoint;
    } else {
        // w 接近零：按无穷远消失点处理，直接取二维方向，不依赖 atPoint。
        projectedDirection = QPointF(projected.x, projected.y);
    }

    // 排除非有限或过短的方向，归一化后才写入输出，失败时保持输出不变。
    const qreal length = QLineF(QPointF(), projectedDirection).length();
    if (!qIsFinite(length) || length < Epsilon)
        return false;
    *direction = projectedDirection / length;
    return true;
}

PerspectivePlane extrudePerpendicularPlane(const PerspectivePlane &source, int edge,
                                           const QPointF &dragPoint,
                                           const QPointF &pressPoint,
                                           const QSize &backgroundSize)
{
    // 步骤 1：检查输入，并初始化新面片的接缝属性；失败时返回当前构造结果。
    PerspectivePlane result;
    if (!isValidEdgeIndex(edge) || backgroundSize.isEmpty() ||
        !qIsFinite(dragPoint.x()) || !qIsFinite(dragPoint.y()) ||
        !qIsFinite(pressPoint.x()) || !qIsFinite(pressPoint.y()))
        return result;
    result.setSurfaceGroupId(source.surfaceGroupId());
    result.setEdgeLocked(0, true); // 新平面的第 0 条边就是与源平面共用的边
    result.quad().setSurfaceCorner(0, source.quad().surfaceCorners()[edge]);
    result.quad().setSurfaceCorner(1, source.quad().surfaceCorners()[(edge + 1) % PerspectiveQuad::CornerCount]);
    // 外侧展开角点先与接缝重合，后续再根据伸出深度展开。
    result.quad().setSurfaceCorner(2, result.quad().surfaceCorners()[1]);
    result.quad().setSurfaceCorner(3, result.quad().surfaceCorners()[0]);
    // 步骤 2：求共享边中点处的法线投影，将鼠标位移分解为有符号伸出距离。
    const QPointF a = source.quad().canvasCorners()[edge];
    const QPointF b = source.quad().canvasCorners()[(edge + 1) % PerspectiveQuad::CornerCount];
    const QPointF midpoint = (a + b) / 2.0;
    QPointF perpendicularAtMidpoint;
    if (!projectedNormalDirection(source, midpoint, backgroundSize, &perpendicularAtMidpoint))
        return result;

    // 点积仅保留法线投影方向上的位移，垂直于该方向的鼠标移动不贡献深度。
    const qreal amount = QPointF::dotProduct(dragPoint - pressPoint,
                                             perpendicularAtMidpoint);
    const QPointF targetMidpoint = midpoint + perpendicularAtMidpoint * amount;
    result.quad().setCanvasCorner(0, a);
    result.quad().setCanvasCorner(1, b);

    if (qAbs(amount) < 2.0) {
        // 拖动太小时保留重合的内外边，交给调用方作为无效候选过滤。
        result.quad().setCanvasCorner(2, b);
        result.quad().setCanvasCorner(3, a);
        return result;
    }

    // 步骤 3：共享边与外侧边在三维中同向，优先用其消失点确定外侧边。
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
            // 两端的法线投影方向可能不同，分别与外侧边所在直线求交。
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

    // 步骤 4：消失点在无穷远、过远或上述构造失败时，尝试与共享边平行的外侧边。
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
            // 仍无法求交则让外侧边退回接缝，返回退化候选而不是修改源平面。
            result.quad().setCanvasCorner(2, b);
            result.quad().setCanvasCorner(3, a);
        }
    }

    // 步骤 5：建立展开坐标。接缝坐标与源面相同，伸出方向取展开边的二维法线。
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
    // 用源面中心判断朝向，确保新面的展开坐标位于接缝另一侧。
    if (QPointF::dotProduct(outward, sourceCenter - seamCenter) > 0.0)
        outward = -outward;
    const qreal canvasDepth =
        (QLineF(result.quad().canvasCorners()[0], result.quad().canvasCorners()[3]).length() +
         QLineF(result.quad().canvasCorners()[1], result.quad().canvasCorners()[2]).length()) / 2.0;
    // 以两侧画布长度的均值估计深度，再按接缝的展开/画布长度比换算，最小取 1。
    const qreal surfaceDepth = qMax(1.0, canvasDepth * surfaceEdgeLength / canvasEdgeLength);
    result.quad().setSurfaceCorner(2, surfaceB + outward * surfaceDepth);
    result.quad().setSurfaceCorner(3, surfaceA + outward * surfaceDepth);
    return result;
}

PerspectivePlane rotatePlaneAroundEdge(const PerspectivePlane &source, int edge,
                                       qreal targetAngle,
                                       const QSize &backgroundSize)
{
    // 先复制源平面，所有失败路径都返回原状态；旋转增量依赖源面记录的当前夹角。
    PerspectivePlane result = source;
    if (!isValidEdgeIndex(edge) || !qIsFinite(targetAngle) || backgroundSize.isEmpty())
        return result;

    // 共享边 edge -> next 是旋转轴；farA、farB 是需要移动的两个对侧角点。
    const int next = (edge + 1) % PerspectiveQuad::CornerCount;
    const int farB = (edge + 2) % PerspectiveQuad::CornerCount;
    const int farA = (edge + 3) % PerspectiveQuad::CornerCount;
    const QPointF seamA = source.quad().canvasCorners()[edge];
    const QPointF seamB = source.quad().canvasCorners()[next];

    // 步骤 1：用两组边线求共享边方向 u 和深度方向 v 的齐次消失点。
    const Vec3 seamLine = joinLines(imagePoint(seamA), imagePoint(seamB));
    const Vec3 outerLine = joinLines(imagePoint(source.quad().canvasCorners()[farA]),
                                     imagePoint(source.quad().canvasCorners()[farB]));
    const Vec3 sideA = joinLines(imagePoint(seamA), imagePoint(source.quad().canvasCorners()[farA]));
    const Vec3 sideB = joinLines(imagePoint(seamB), imagePoint(source.quad().canvasCorners()[farB]));
    const Vec3 vanishingU = meetLines(seamLine, outerLine);
    const Vec3 vanishingV = meetLines(sideA, sideB);
    if (dot(vanishingU, vanishingU) < 1e-12 || dot(vanishingV, vanishingV) < 1e-12)
        return result;

    // 步骤 2：估计相机内参，以背景中心为主点；焦距无法可靠估计时使用经验值。
    const qreal cx = backgroundSize.width() / 2.0;
    const qreal cy = backgroundSize.height() / 2.0;
    const qreal imageExtent = qMax(backgroundSize.width(), backgroundSize.height());
    qreal focalLength = imageExtent * 1.2;
    QPointF vuImage;
    QPointF vvImage;
    if (toImagePoint(vanishingU, &vuImage) && toImagePoint(vanishingV, &vvImage))
        focalLength = focalFromOrthogonalVanishingPoints(vuImage, vvImage, backgroundSize);
    const CameraFrame frame{focalLength, cx, cy};

    // 恢复旋转轴和深度的三维方向，叉乘得到源平面法线。
    Vec3 axis = vanishingDirection(vanishingU, frame);
    Vec3 depthDirection = vanishingDirection(vanishingV, frame);
    if (!normalize(&axis) || !normalize(&depthDirection))
        return result;
    Vec3 normal = cross(axis, depthDirection);
    if (!normalize(&normal))
        return result; // 两个消失方向重合，无法定义子平面

    // 步骤 3：反投影到 normal·X = h。取 h = normal·raySeamA，使 a 等于该射线向量。
    // 此尺度选择不改变最终图像位置；射线与平面的交点为 ray * h / (normal·ray)。
    const Vec3 raySeamA = imageRay(seamA, frame);
    const double h = dot(normal, raySeamA);
    if (qAbs(h) < 1e-9)
        return result; // 子平面几乎穿过相机中心
    auto onPlane = [&](const QPointF &p, Vec3 *out) {
        const Vec3 ray = imageRay(p, frame);
        const double denominator = dot(normal, ray);
        // 射线近乎平行于平面时无法求出可靠的有限交点。
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
        !onPlane(source.quad().canvasCorners()[farA], &outerA) ||
        !onPlane(source.quad().canvasCorners()[farB], &outerB))
        return result;
    // 消失点齐次符号任意：把轴统一成 seamA -> seamB，夹角正方向才不随绕向变化。
    if (dot(b - a, axis) < 0.0)
        axis = axis * -1.0;

    // 步骤 4：目标夹角减去当前夹角得到增量，并归约到 (-180°, 180°]。
    // 在三维中使用完整 Rodrigues 公式，保留平行于轴的分量，使一般四边形也按刚体旋转。
    qreal delta = std::fmod(targetAngle, 360.0) - std::fmod(source.angleToParentDegrees(), 360.0);
    if (delta > 180.0)
        delta -= 360.0;
    if (delta <= -180.0)
        delta += 360.0;
    const qreal radians = qDegreesToRadians(delta);
    const double cosine = qCos(radians);
    const double sine = qSin(radians);
    auto rotateAroundSeam = [&](const Vec3 &v) {
        // 取 v × axis 为正方向，使 0° 恰好是完全展开、与父平面共面的状态。
        return v * cosine + cross(v, axis) * sine + axis * (dot(axis, v) * (1.0 - cosine));
    };
    // 分别以共享边两端为基点旋转对侧角点，两基点在同一旋转轴上，轴端点保持不动。
    const Vec3 movedA = a + rotateAroundSeam(outerA - a);
    const Vec3 movedB = b + rotateAroundSeam(outerB - b);

    // 步骤 5：投影回画布；相机后方、非有限或绝对坐标超过 1e7 的点都会导致失败。
    QPointF projectedA;
    QPointF projectedB;
    if (!projectPoint(movedA, frame, &projectedA) || !projectPoint(movedB, frame, &projectedB))
        return result;
    result.quad().setCanvasCorner(farA, projectedA);
    result.quad().setCanvasCorner(farB, projectedB);

    // 步骤 6：归一化记录的目标角度并设置自定义标记。展开坐标保持不变，纹理仍跟随角点。
    // 正的整周角保留为 360°，其余角度落在 [0°, 360°) 内。
    qreal normalizedAngle = std::fmod(targetAngle, 360.0);
    if (qFuzzyIsNull(normalizedAngle) && targetAngle > 0.0)
        normalizedAngle = 360.0;
    else if (normalizedAngle < 0.0)
        normalizedAngle += 360.0;
    result.setAngleToParentDegrees(normalizedAngle);
    result.setHasCustomAngle(true);
    // 最终候选必须通过几何有效性检查；否则连同角度和自定义标记一起回退。
    if (!result.quad().isValid())
        return source;
    return result;
}

bool PerspectivePlane::controlPointEditable(int handle, bool extrude) const
{
    if (handle < 0 || handle >= 8)
        return false;
    if (handle < 4)
        return !isEdgeLocked(handle) && !isEdgeLocked((handle + 3) % 4);
    const int edge = handle - 4;
    if (extrude)
        return !isEdgeLocked(edge);
    return !isEdgeLocked(edge) && !isEdgeLocked((edge + 1) % 4)
        && !isEdgeLocked((edge + 3) % 4);
}

bool PerspectivePlane::preservesLockedEdges(const PerspectivePlane &candidate) const
{
    for (int edge = 0; edge < 4; ++edge) {
        if (!isEdgeLocked(edge))
            continue;
        for (int corner : {edge, (edge + 1) % 4})
            if (QLineF(quad().canvasCorners()[corner], candidate.quad().canvasCorners()[corner]).length() > 1e-7)
                return false;
    }
    return true;
}
