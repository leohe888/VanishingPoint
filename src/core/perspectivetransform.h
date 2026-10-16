#pragma once

#include <QPolygonF>
#include <QTransform>
#include <QRectF>

class PerspectiveTransform
{
public:
    PerspectiveTransform() = default;
    PerspectiveTransform(const QPolygonF &sourceQuad, const QPolygonF &targetQuad);
    bool isValid() const { return m_valid; }
    // Safe mapping preserves output on failure; extrapolation stays on Qt's visible branch.
    bool mapForward(const QPointF &point, QPointF *result) const;
    bool mapInverse(const QPointF &point, QPointF *result) const;
    // Checks the source rectangle before projecting a brush footprint or stroke segment.
    bool canProject(const QRectF &domain) const;
    const QTransform &forward() const { return m_forward; }

private:
    static bool mapPoint(const QTransform &transform, const QPointF &point, QPointF *result);
    QTransform m_forward;
    QTransform m_inverse;
    bool m_valid = false;
};
