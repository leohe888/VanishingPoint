#pragma once

#include "perspectiveplane.h"

#include <QColor>
#include <QImage>
#include <QRect>

class QPainter;

// 画笔工具：把笔触烘焙进绘画层。一笔锚定在按下点所在的面片上，从而随透视
// 自然缩短；平面之外则以整张图像为基准面，得到画布尺度的普通圆形笔刷。
class BrushTool
{
public:
    // —— 笔刷参数（越界值钳到合法区间） ——
    void setDiameter(int value) { m_diameter = qBound(1, value, 500); }
    void setHardness(int value) { m_hardness = qBound(0, value, 100); }
    void setOpacity(int value) { m_opacity = qBound(1, value, 100); }
    void setColor(const QColor &color) { m_brushColor = color; }
    int diameter() const { return m_diameter; }
    int hardness() const { return m_hardness; }
    int opacity() const { return m_opacity; }
    QColor color() const { return m_brushColor; }

    // 起笔：锚定按下点对应的面片并落下第一个笔触点，返回画布脏矩形。
    // 锚定失败时返回空矩形，且不进入落笔状态。
    QRect begin(QImage &layer, const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                const QPointF &point);
    // 续笔：从上一位置向目标位置补间落点，返回画布脏矩形。
    QRect move(QImage &layer, const QPointF &point);
    void end() { m_drawing = false; }
    bool drawing() const { return m_drawing; }

    // 在光标处预览即将落下的笔触点（所见即所得）。
    // 不改变任何交互状态，与真实落笔使用同一绘制逻辑。
    void renderPreview(QPainter &painter, const QVector<PerspectivePlane> &planes,
                       const QSize &canvasSize, const QPointF &point) const;

private:
    // 解析点对应的可绘制面片（quad）与其归一化 UV。
    // 面片退化，或点落在该面片地平线之外时返回 false。
    static bool resolveTarget(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                              const QPointF &point, PerspectiveQuad *quad, QPointF *uv);

    QRect applyDab(QPainter &painter, const PerspectiveQuad &quad, const QPointF &uv) const;
    QRect drawStrokeTo(QImage &layer, const QPointF &uv);

    PerspectiveQuad m_quad;          // 本笔锚定的面片
    QPointF m_lastUv;
    QColor m_brushColor = QColor("#e85d4a");
    int m_diameter = 42;
    int m_hardness = 75;
    int m_opacity = 100;
    bool m_drawing = false;
};
