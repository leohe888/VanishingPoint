#pragma once

#include "perspectiveplane.h"

#include <QPointF>
#include <QString>
#include <QVector>

// 创建平面工具：依次收集用户点击的 4 个角点，凑齐后生成一个透视平面。
// 与 PlaneEditTool 一样只维护交互状态，不接触文档和视图；
// 橡皮筋预览由 SceneRenderer 根据 points() 与当前光标位置绘制。
class PlaneCreateTool
{
public:
    static constexpr int CornerCount = PerspectiveFacet::CornerCount;

    void addPoint(const QPointF &point);           // 落下一个角点；已凑齐时忽略
    void removeLastPoint();                        // 回退最后一个角点；已空时忽略
    void reset() { m_points.clear(); }             // 放弃本次创建
    bool creating() const { return !m_points.isEmpty(); }
    bool finished() const { return m_points.size() == CornerCount; }
    const QVector<QPointF> &points() const { return m_points; }

    // 用已落下的 4 个角点构造平面；角点是否有效由调用方用 isValid() 判断。
    PerspectivePlane makePlane(int surfaceGroupId) const;

private:
    QVector<QPointF> m_points;
};
