#pragma once

#include "clonestampengine.h"
#include "perspectiveplane.h"

class QPainter;

// 图章工具：先用 Alt+单击取一个源点，之后在画布上拖动即可把源内容仿制过来。
// 源点与落点各自解析成一块面片（平面之外退化为整张图像），偏移记在展开曲面上，
// 因此仿制结果跟随透视，也能跨过共用曲面的相邻平面。
// 对齐模式：偏移一经确定就跨笔保留，源点随光标一起移动；
// 非对齐模式：每笔按落点重新锚定偏移，抬笔后源点归位。
class CloneTool
{
public:
    // —— 笔刷参数（转发给引擎，越界值由引擎钳到合法区间） ——
    void setDiameter(int value) { m_engine.setDiameter(value); }
    void setHardness(int value) { m_engine.setHardness(value); }
    void setOpacity(int value) { m_engine.setOpacity(value); }
    int diameter() const { return m_engine.diameter(); }
    int hardness() const { return m_engine.hardness(); }
    int opacity() const { return m_engine.opacity(); }
    void setAligned(bool value);
    bool aligned() const { return m_aligned; }

    bool hasSource() const { return m_hasSource; }
    bool drawing() const { return m_drawing; }
    QPointF marker() const { return m_marker; } // 当前的取样位置指示（画面坐标）
    // 取源点：解析点所在面片并记下它在展开曲面上的位置。
    bool pickSource(const QVector<PerspectivePlane> &planes, const QSize &canvasSize, const QPointF &point);

    // 起笔：解析落点面片，按对齐模式确定偏移并落下第一个笔触点，返回画布脏矩形。
    // 未取源、面片不可解析或绘画层无效时返回空矩形，且不进入落笔状态。
    QRect begin(QImage &layer, const QImage &source, const QVector<PerspectivePlane> &planes,
                const QSize &canvasSize, const QPointF &point);
    // 续笔：从上一位置向目标位置补间落点，返回画布脏矩形。
    QRect move(QImage &layer, const QPointF &point);
    void end();

    // 光标移动：更新取样位置指示。源点已归位时只显示它本身。
    void hover(const QVector<PerspectivePlane> &planes, const QSize &canvasSize, const QPointF &point);
    // 在光标处预览即将仿制过来的内容（所见即所得）：复用引擎的逐像素取样，
    // 与真实落笔完全一致，取样位置始终落在源点十字上，但不改变源点与落笔状态。
    void renderPreview(QPainter &painter, const QImage &source, const QVector<PerspectivePlane> &planes,
                       const QSize &canvasSize, const QPointF &point);

private:
    // 目标面片的展开映射：落笔期间沿用锚定面片，否则取光标所在面片。
    PerspectiveTransform targetAt(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                                 const QPointF &point) const;
    // 本次落笔应有的偏移：对齐模式沿用已锁定的偏移，其余按落点重新锚定。
    QPointF anchoredOffset(const QPointF &position) const;
    QPointF originalMarker() const; // 未加偏移时的源点位置

    CloneStampEngine m_engine;
    PerspectiveTransform m_sourceMapping, m_targetMapping;
    bool m_hasSource = false, m_hasOffset = false, m_aligned = true, m_drawing = false;
    QPointF m_source; // 源点在展开曲面上的位置
    QPointF m_offset; // 源相对落点的展开坐标偏移
    QPointF m_marker; // 画面上的取样位置指示
};
