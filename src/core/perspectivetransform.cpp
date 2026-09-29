#include "perspectivetransform.h"
#include <cmath>

PerspectiveTransform::PerspectiveTransform(const QPolygonF &sourceQuad, const QPolygonF &targetQuad)
{
    if (sourceQuad.size() != 4 || targetQuad.size() != 4)
        return;
    for (const QPolygonF &quad : {sourceQuad, targetQuad})
        for (const QPointF &p : quad)
            if (!qIsFinite(p.x()) || !qIsFinite(p.y()))
                return;
    if (!QTransform::quadToQuad(sourceQuad, targetQuad, m_forward))
        return;
    m_inverse = m_forward.inverted(&m_valid);
}

bool PerspectiveTransform::mapForward(const QPointF &point, QPointF *result) const
{
    if (!m_valid || !result)
        return false;
    return mapPoint(m_forward, point, result);
}

bool PerspectiveTransform::mapInverse(const QPointF &point, QPointF *result) const
{
    if (!m_valid || !result)
        return false;
    return mapPoint(m_inverse, point, result);
}

bool PerspectiveTransform::mapPoint(const QTransform &transform, const QPointF &point, QPointF *result)
{
    if (!result || !qIsFinite(point.x()) || !qIsFinite(point.y()))
        return false;
    const qreal xw = transform.m13() * point.x(), yw = transform.m23() * point.y();
    const qreal w = xw + yw + transform.m33();
    // Qt's rasterizer clamps negative/near-zero w instead of drawing that branch.
    // Keep tool coordinates on the same visible branch as rendering.
    if (!qIsFinite(w) || w <= 1e-6 * qMax(1.0, qAbs(xw) + qAbs(yw) + qAbs(transform.m33())))
        return false;
    const QPointF mapped((transform.m11()*point.x()+transform.m21()*point.y()+transform.m31())/w,
                         (transform.m12()*point.x()+transform.m22()*point.y()+transform.m32())/w);
    if (!qIsFinite(mapped.x()) || !qIsFinite(mapped.y())
        || qAbs(mapped.x()) > 1e9 || qAbs(mapped.y()) > 1e9)
        return false;
    *result = mapped;
    return true;
}

bool PerspectiveTransform::mapsDomain(const QTransform &transform, const QRectF &domain)
{
    qreal sign = 0;
    for (const QPointF &p : {domain.topLeft(), domain.topRight(), domain.bottomLeft(), domain.bottomRight()}) {
        QPointF mapped;
        if (!mapPoint(transform, p, &mapped))
            return false;
        const qreal w = transform.m13()*p.x()+transform.m23()*p.y()+transform.m33();
        if (sign != 0 && (w > 0) != (sign > 0))
            return false;
        sign = w;
    }
    return true;
}
