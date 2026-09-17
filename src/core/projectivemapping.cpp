#include "projectivemapping.h"

#include <QtMath>

// 根据源四边形与目标四边形，计算正向/反向的透视变换矩阵，并验证其有效性。
ProjectiveMapping::ProjectiveMapping(const QPolygonF &sourceQuad, const QPolygonF &targetQuad)
{
    if (sourceQuad.size() != 4 || targetQuad.size() != 4)
        return;

    // 计算两个四边形的中心作为参考点
    for (int i = 0; i < 4; ++i) {
        m_sourceReference += sourceQuad[i] / 4;
        m_targetReference += targetQuad[i] / 4;
    }

    // 计算正向变换
    if (!QTransform::quadToQuad(sourceQuad, targetQuad, m_forward))
        return;

    auto normalize = [](const QTransform &t, const QPointF &reference) {
        const qreal w = t.m13() * reference.x() + t.m23() * reference.y() + t.m33();
        return w >= 0 ? t : QTransform(-t.m11(), -t.m12(), -t.m13(), -t.m21(), -t.m22(), -t.m23(),
                                       -t.m31(), -t.m32(), -t.m33());
    };

    // 归一化正向变换的符号
    m_forward = normalize(m_forward, m_sourceReference);

    // 计算逆向变换
    m_inverse = m_forward.inverted(&m_valid);

    // 归一化逆向变换的符号
    m_inverse = normalize(m_inverse, m_targetReference);

    // 遍历四个顶点：
    // 对每个 source 顶点，用正向变换 + 可见性检查，看它是否能映射到 target 平面。
    // 对每个 target 顶点，用逆向变换 + 可见性检查，看它是否能映射回 source 平面。
    QPointF mapped;
    for (int i = 0; m_valid && i < 4; ++i)
        m_valid = mapVisible(m_forward, sourceQuad[i], m_sourceReference, &mapped)
                  && mapVisible(m_inverse, targetQuad[i], m_targetReference, &mapped);
}

// 射影平面被地平线分成两个半平面
bool ProjectiveMapping::mapVisible(const QTransform &transform, const QPointF &point,
                                   const QPointF &reference, QPointF *result)
{
    if (!result || !qIsFinite(point.x()) || !qIsFinite(point.y()))
        return false;

    // [x']   [m11 m21 m31]   [x]
    // [y'] = [m12 m22 m32] * [y]
    // [w ]   [m13 m23 m33]   [1]
    // w = m13 * x + m23 * y + m33
    auto denominator = [&transform](const QPointF &p) {
        return transform.m13() * p.x() + transform.m23() * p.y() + transform.m33();
    };

    // 计算参考点和当前点的 w
    const qreal referenceW = denominator(reference), w = denominator(point);

    if (!qIsFinite(referenceW) || !qIsFinite(w) || referenceW * w <= 0
        || qAbs(w) <= qAbs(referenceW) * 1e-6)
        return false;

    // 手动做齐次除法，因为QTransform::map 对负分母有绘制裁剪语义，几何计算必须直接做齐次除法。
    const QPointF mapped((transform.m11() * point.x() + transform.m21() * point.y() + transform.m31()) / w,
                          (transform.m12() * point.x() + transform.m22() * point.y() + transform.m32()) / w);
    if (!qIsFinite(mapped.x()) || !qIsFinite(mapped.y()))
        return false;
    *result = mapped;
    return true;
}

bool ProjectiveMapping::mapForward(const QPointF &point, QPointF *result) const
{
    return m_valid && mapVisible(m_forward, point, m_sourceReference, result);
}

bool ProjectiveMapping::mapInverse(const QPointF &point, QPointF *result) const
{
    return m_valid && mapVisible(m_inverse, point, m_targetReference, result);
}
