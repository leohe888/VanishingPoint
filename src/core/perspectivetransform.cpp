#include "perspectivetransform.h"

PerspectiveTransform::PerspectiveTransform(const QPolygonF &sourceQuad, const QPolygonF &targetQuad)
{
    if (sourceQuad.size() != 4 || targetQuad.size() != 4)
        return;
    if (!QTransform::quadToQuad(sourceQuad, targetQuad, m_forward))
        return;
    m_inverse = m_forward.inverted(&m_valid);
}

bool PerspectiveTransform::mapForward(const QPointF &point, QPointF *result) const
{
    if (!m_valid || !result)
        return false;
    *result = m_forward.map(point);
    return true;
}

bool PerspectiveTransform::mapInverse(const QPointF &point, QPointF *result) const
{
    if (!m_valid || !result)
        return false;
    *result = m_inverse.map(point);
    return true;
}
