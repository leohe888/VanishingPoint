#pragma once

#include <QPolygonF>
#include <QTransform>
#include <QRectF>

class PerspectiveTransform
{
public:
    PerspectiveTransform() = default;
    PerspectiveTransform(const QPolygonF &sourceQuad, const QPolygonF &targetQuad);
    // Safe mapping preserves output on failure; extrapolation stays on Qt's visible branch.
    static bool mapPoint(const QTransform &transform, const QPointF &point, QPointF *result);
    static bool mapsDomain(const QTransform &transform, const QRectF &domain);
    bool isValid() const { return m_valid; }
    bool mapForward(const QPointF &point, QPointF *result) const;
    bool mapInverse(const QPointF &point, QPointF *result) const;
    const QTransform &forward() const { return m_forward; }

private:
    QTransform m_forward;
    QTransform m_inverse;
    bool m_valid = false;
};
