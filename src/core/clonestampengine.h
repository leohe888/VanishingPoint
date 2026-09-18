#pragma once

#include <QImage>
#include <QPointF>
#include <QTransform>

// 图章引擎：按源点偏移逐像素取样，把源内容烘焙进画布同尺寸的绘画层。
// 目标与源各用自己的展开映射，偏移因此在曲面上成立——投影回画面后自带
// 透视缩短，也能跨过共用曲面的相邻平面。取样位置到源图之外时留空。
class CloneStampEngine
{
public:
    // —— 笔刷参数（取值区间与 ToolOptionsBar 一致，越界值会被钳住） ——
    void setDiameter(int value) { m_diameter = qBound(1, value, 500); }  // 直径 1~500（图像像素）
    void setHardness(int value) { m_hardness = qBound(0, value, 100); }  // 硬度 0~100（百分比）
    void setOpacity(int value) { m_opacity = qBound(1, value, 100); }    // 不透明度 1~100（百分比）
    int diameter() const { return m_diameter; }
    int hardness() const { return m_hardness; }
    int opacity() const { return m_opacity; }

    // 在目标位置（目标面片的展开坐标）开始一笔并落下第一个笔触点，返回画布脏矩形。
    // 源图在落笔瞬间快照，整笔都取自同一份内容。
    QRect beginStroke(QImage &layer, const QImage &source, const QTransform &targetToCanvas,
                      const QTransform &sourceToCanvas, const QPointF &offset,
                      const QPointF &position);
    // 从上一位置向目标位置补间落点，返回画布脏矩形。
    QRect drawStrokeTo(QImage &layer, const QPointF &position);
    void endStroke() { m_source = QImage(); }

    // —— 光标预览支持 ——
    // 只写入取样所需的源与映射，不记落点、不进入落笔状态，
    // 因此可与真实落笔共用同一台引擎而互不干扰。
    void setPreview(const QImage &source, const QTransform &targetToCanvas,
                    const QTransform &sourceToCanvas, const QPointF &offset);
    // position 处一个笔触点在画布上的矩形（已裁到源图范围）。
    QRect dabRect(const QPointF &position) const;
    // 把 position 处一个笔触点的源取样写入 dab（尺寸须等于 dabRect）；
    // 返回是否真的写入了非透明像素。
    bool renderDab(QImage &dab, const QRect &area, const QPointF &position) const;

private:
    QRect applyDab(QImage &layer, const QPointF &position);

    QImage m_source;
    QTransform m_targetToCanvas, m_canvasToTarget, m_sourceToCanvas;
    QPointF m_offset, m_lastPosition;
    int m_diameter = 42;   // 笔刷直径（图像像素）
    int m_hardness = 75;   // 笔刷硬度（百分比）
    int m_opacity = 100;   // 笔刷不透明度（百分比）
};
