#include "perspectivequad.h"

#include <QLineF>
#include <QTransform>
#include <QtMath>

#include <utility>

namespace {

constexpr qreal Epsilon = 1e-6;

qreal pointToSegmentDistance(const QPointF &point, const QPointF &start,
                             const QPointF &end)
{
    const QPointF segment = end - start;
    const qreal squaredLength = QPointF::dotProduct(segment, segment);
    // 将投影参数限制在 [0, 1]，求到线段而非无限直线的距离；退化线段按起点处理。
    const qreal position = squaredLength < Epsilon
        ? 0.0
        : qBound(0.0, QPointF::dotProduct(point - start, segment) / squaredLength, 1.0);
    return QLineF(point, start + segment * position).length();
}

} // namespace

PerspectiveQuad::PerspectiveQuad(Corners canvasCorners, Corners surfaceCorners)
    : m_canvasCorners(std::move(canvasCorners)),
      m_surfaceCorners(std::move(surfaceCorners))
{
}

void PerspectiveQuad::setCanvasCorner(int index, const QPointF &point)
{
    Q_ASSERT(index >= 0 && index < CornerCount);
    if (index >= 0 && index < CornerCount)
        m_canvasCorners[index] = point;
}

void PerspectiveQuad::setSurfaceCorner(int index, const QPointF &point)
{
    Q_ASSERT(index >= 0 && index < CornerCount);
    if (index >= 0 && index < CornerCount)
        m_surfaceCorners[index] = point;
}

QPolygonF PerspectiveQuad::canvasPolygon() const
{
    return {m_canvasCorners[0], m_canvasCorners[1],
            m_canvasCorners[2], m_canvasCorners[3]};
}

QPolygonF PerspectiveQuad::surfacePolygon() const
{
    return {m_surfaceCorners[0], m_surfaceCorners[1],
            m_surfaceCorners[2], m_surfaceCorners[3]};
}

QVector<QPointF> PerspectiveQuad::controlPoints() const
{
    return {m_canvasCorners[0], m_canvasCorners[1],
            m_canvasCorners[2], m_canvasCorners[3],
            (m_canvasCorners[0] + m_canvasCorners[1]) / 2.0,
            (m_canvasCorners[1] + m_canvasCorners[2]) / 2.0,
            (m_canvasCorners[2] + m_canvasCorners[3]) / 2.0,
            (m_canvasCorners[3] + m_canvasCorners[0]) / 2.0};
}

bool PerspectiveQuad::isValid() const
{
    for (const QPointF &corner : m_canvasCorners) {
        if (!qIsFinite(corner.x()) || !qIsFinite(corner.y()))
            return false;
    }

    qreal windingSign = 0.0;
    qreal twiceArea = 0.0;
    for (int i = 0; i < CornerCount; ++i) {
        const QPointF a = m_canvasCorners[i];
        const QPointF b = m_canvasCorners[(i + 1) % CornerCount];
        const QPointF c = m_canvasCorners[(i + 2) % CornerCount];
        // 排除过短的边，避免面片缩小到难以编辑的程度。
        if (QLineF(a, b).length() < 8.0)
            return false;
        const QPointF ab = b - a;
        const QPointF bc = c - b;
        const qreal cross = ab.x() * bc.y() - ab.y() * bc.x();
        // 相邻边不能接近共线，且所有转角须同向，以排除凹四边形和自交四边形。
        if (qAbs(cross) < 4.0)
            return false;
        const qreal sign = cross > 0.0 ? 1.0 : -1.0;
        if (i == 0)
            windingSign = sign;
        else if (sign != windingSign)
            return false;
        // 鞋带公式累加有向面积的两倍，兼容两种角点绕序。
        twiceArea += a.x() * b.y() - b.x() * a.y();
    }
    // 面积至少为 100 个画布坐标单位的平方，避免面片过小。
    if (qAbs(twiceArea) < 100.0)
        return false;

    const QPolygonF unit{QPointF(0, 0), QPointF(1, 0),
                         QPointF(1, 1), QPointF(0, 1)};
    QTransform transform;
    if (!QTransform::quadToQuad(unit, canvasPolygon(), transform))
        return false;

    // 透视映射需要除以齐次分母 w。w 在 UV 上是线性函数，四角非零且同号
    // 可保证整个单位正方形内不会出现 w = 0，避免投影跨越无穷远。
    qreal denominatorSign = 0.0;
    for (const QPointF &uv : unit) {
        const qreal w = transform.m13() * uv.x() +
                        transform.m23() * uv.y() + transform.m33();
        if (!qIsFinite(w) || qAbs(w) < 1e-5)
            return false;
        const qreal sign = w > 0.0 ? 1.0 : -1.0;
        if (denominatorSign == 0.0)
            denominatorSign = sign;
        else if (sign != denominatorSign)
            return false;
    }
    return true;
}

bool PerspectiveQuad::containsCanvasPoint(const QPointF &point) const
{
    return canvasPolygon().containsPoint(point, Qt::OddEvenFill);
}

int PerspectiveQuad::controlPointIndexAt(const QPointF &point, qreal tolerance) const
{
    if (!qIsFinite(point.x()) || !qIsFinite(point.y()) ||
        !qIsFinite(tolerance) || tolerance < 0.0)
        return -1;
    const QVector<QPointF> points = controlPoints();
    for (int i = 0; i < points.size(); ++i) {
        if (QLineF(points[i], point).length() <= tolerance)
            return i;
    }
    return -1;
}

int PerspectiveQuad::edgeIndexAt(const QPointF &point, qreal tolerance) const
{
    if (!qIsFinite(point.x()) || !qIsFinite(point.y()) ||
        !qIsFinite(tolerance) || tolerance < 0.0)
        return -1;
    for (int i = 0; i < CornerCount; ++i) {
        if (pointToSegmentDistance(point, m_canvasCorners[i],
                                   m_canvasCorners[(i + 1) % CornerCount]) <= tolerance)
            return i;
    }
    return -1;
}

PerspectiveTransform PerspectiveQuad::surfaceToCanvasTransform() const
{
    return {surfacePolygon(), canvasPolygon()};
}

PerspectiveTransform PerspectiveQuad::uvToCanvasTransform() const
{
    return {{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)},
            canvasPolygon()};
}
