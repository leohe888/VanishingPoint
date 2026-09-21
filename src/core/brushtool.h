#pragma once

#include "paintengine.h"
#include "perspectiveplane.h"

class QPainter;

// 画笔工具：把笔触烘焙进绘画层。一笔锚定在按下点所在的面片上，从而随透视
// 自然缩短；平面之外则以整张图像为基准面，得到画布尺度的普通圆形笔刷。
class BrushTool
{
public:
    // —— 笔刷参数（转发给引擎，越界值由引擎钳到合法区间） ——
    void setDiameter(int value) { m_engine.setDiameter(value); }
    void setHardness(int value) { m_engine.setHardness(value); }
    void setOpacity(int value) { m_engine.setOpacity(value); }
    void setColor(const QColor &color) { m_engine.setColor(color); }
    int diameter() const { return m_engine.diameter(); }
    int hardness() const { return m_engine.hardness(); }
    int opacity() const { return m_engine.opacity(); }
    QColor color() const { return m_engine.color(); }

    // 起笔：锚定按下点对应的面片并落下第一个笔触点，返回画布脏矩形。
    // 锚定失败时返回空矩形，且不进入落笔状态。
    QRect begin(QImage &layer, const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                const QPointF &point);
    // 续笔：从上一位置向目标位置补间落点，返回画布脏矩形。
    QRect move(QImage &layer, const QPointF &point);
    void end() { m_drawing = false; }
    bool drawing() const { return m_drawing; }

    // 在光标处预览即将落下的笔触点（所见即所得）。
    // 不改变任何交互状态，可与真实落笔共用同一台引擎。
    void renderPreview(QPainter &painter, const QVector<PerspectivePlane> &planes,
                       const QSize &canvasSize, const QPointF &point) const;

private:
    // 解析点对应的可绘制面片（facet）与其归一化 UV。
    // 面片退化，或点落在该面片地平线之外时返回 false。
    static bool resolveTarget(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                              const QPointF &point, PerspectiveFacet *facet, QPointF *uv);

    PaintEngine m_engine;
    PerspectiveFacet m_facet;          // 本笔锚定的面片
    bool m_drawing = false;
};
