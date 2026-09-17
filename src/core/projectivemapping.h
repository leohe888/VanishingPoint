#pragma once

#include <QPolygonF>
#include <QTransform>

class ProjectiveMapping
{
public:
    ProjectiveMapping() = default;
    ProjectiveMapping(const QPolygonF &sourceQuad, const QPolygonF &targetQuad);
    bool isValid() const { return m_valid; }
    bool mapForward(const QPointF &point, QPointF *result) const;
    bool mapInverse(const QPointF &point, QPointF *result) const;
    const QTransform &forward() const { return m_forward; }
    const QTransform &inverse() const { return m_inverse; }
    static bool mapVisible(const QTransform &transform, const QPointF &point,
                           const QPointF &reference, QPointF *result);

private:
    QTransform m_forward, m_inverse;
    QPointF m_sourceReference, m_targetReference;
    bool m_valid = false;
};
