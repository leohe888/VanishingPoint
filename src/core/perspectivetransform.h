#pragma once

#include <QPolygonF>
#include <QTransform>

class PerspectiveTransform
{
public:
    PerspectiveTransform() = default;
    PerspectiveTransform(const QPolygonF &sourceQuad, const QPolygonF &targetQuad);
    bool isValid() const { return m_valid; }
    bool mapForward(const QPointF &point, QPointF *result) const;
    bool mapInverse(const QPointF &point, QPointF *result) const;
    const QTransform &forward() const { return m_forward; }
    const QTransform &inverse() const { return m_inverse; }

private:
    QTransform m_forward;
    QTransform m_inverse;
    bool m_valid = false;
};
