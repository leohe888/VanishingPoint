#include "perspectivequad.h"

#include <QLineF>
#include <QTransform>
#include <QtMath>

#include <utility>
#include <cmath>

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
    return QPolygonF(QVector<QPointF>{m_canvasCorners[0], m_canvasCorners[1],
                                     m_canvasCorners[2], m_canvasCorners[3]});
}

QPolygonF PerspectiveQuad::surfacePolygon() const
{
    return QPolygonF(QVector<QPointF>{m_surfaceCorners[0], m_surfaceCorners[1],
                                     m_surfaceCorners[2], m_surfaceCorners[3]});
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
    // 实际面积是鞋带和的一半。
    if (qAbs(twiceArea) / 2.0 < MinimumCanvasArea)
        return false;

    const QPolygonF unit(QVector<QPointF>{QPointF(0, 0), QPointF(1, 0),
                                        QPointF(1, 1), QPointF(0, 1)});
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

PerspectiveQuad::Status PerspectiveQuad::status(const QSize &backgroundSize) const
{
    if (!isProjectable()) return Status::InvalidAspectRatio;
    // Same camera assumptions as plane extrusion: square pixels, principal point
    // at image center. Adobe does not publish its classification thresholds.
    const QRectF bounds = canvasPolygon().boundingRect();
    const QPointF center = backgroundSize.isEmpty() ? bounds.center()
        : QPointF(backgroundSize.width() / 2.0, backgroundSize.height() / 2.0);
    const qreal extent = backgroundSize.isEmpty() ? qMax(bounds.width(), bounds.height())
        : qMax(backgroundSize.width(), backgroundSize.height());
    const QPolygonF unit(QVector<QPointF>{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)});
    QPolygonF normalized;
    for (const QPointF &point : canvasPolygon()) normalized.append((point - center) / extent);
    QTransform h;
    if (!QTransform::quadToQuad(unit, normalized, h)) return Status::InvalidAspectRatio;
    const qreal ux = h.m11(), uy = h.m12(), uw = h.m13();
    const qreal vx = h.m21(), vy = h.m22(), vw = h.m23();
    const qreal un = std::hypot(std::hypot(ux, uy), uw), vn = std::hypot(std::hypot(vx, vy), vw);
    if (!qIsFinite(un) || !qIsFinite(vn) || un < 1e-12 || vn < 1e-12)
        return Status::UnresolvedVanishingPoints;
    const bool uInfinite = qAbs(uw / un) < 1e-9, vInfinite = qAbs(vw / vn) < 1e-9;
    qreal focalSquared = 1.2 * 1.2;
    if (uInfinite || vInfinite) {
        // Parallel opposite edges give a valid direction at infinity, not an
        // automatic warning. Inconsistent orthogonality cannot be resolved.
        if (qAbs((ux * vx + uy * vy) / (un * vn)) > .02)
            return Status::UnresolvedVanishingPoints;
    } else {
        focalSquared = -(ux * vx + uy * vy) / (uw * vw);
        if (!qIsFinite(focalSquared)) return Status::UnresolvedVanishingPoints;
        if (focalSquared <= 0) return Status::InvalidAspectRatio;
        if (focalSquared < .08 * .08 || focalSquared > 20 * 20)
            return Status::UnresolvedVanishingPoints;
    }
    const qreal width = std::sqrt(ux * ux + uy * uy + focalSquared * uw * uw);
    const qreal height = std::sqrt(vx * vx + vy * vy + focalSquared * vw * vw);
    const qreal aspect = width / height;
    if (!qIsFinite(aspect) || aspect < 1e-4 || aspect > 1e4)
        return Status::InvalidAspectRatio;
    return Status::Valid;
}

QColor PerspectiveQuad::guideColor(bool, const QSize &backgroundSize) const
{
    QColor color;
    switch (status(backgroundSize)) {
    case Status::Valid: color = QColor("#017AFF"); break;
    case Status::UnresolvedVanishingPoints: color = QColor("#E6B94A"); break;
    case Status::InvalidAspectRatio: color = QColor("#E60012"); break;
    }
    return color;
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

PerspectiveTransform PerspectiveQuad::surfaceToCanvasTransform() const
{
    return {surfacePolygon(), canvasPolygon()};
}

bool PerspectiveQuad::isProjectable() const
{
    if (!isValid())
        return false;
    for (const QPointF &p : m_surfaceCorners)
        if (!qIsFinite(p.x()) || !qIsFinite(p.y()))
            return false;
    return surfaceToCanvasTransform().isValid();
}
