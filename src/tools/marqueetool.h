#pragma once

#include "core/floatingimage.h"
#include "core/perspectiveplane.h"

#include <QPainterPath>
#include <QRectF>

// 选区使用展开曲面坐标和面片快照；不依赖画布、文档或撤销历史。
class MarqueeTool
{
public:
    enum class Action { None, Create, Move, Fill };

    bool beginCreate(const QVector<PerspectivePlane> &planes, const QPointF &point);
    bool beginMove(const QPointF &point);
    bool beginFill(const QPointF &point, const QImage &sampleSource, const QImage &paintBefore);
    // Fill 模式重建 paintLayer 预览并返回脏区域；其他模式只更新选区。
    QRect update(const QPointF &point, Qt::KeyboardModifiers modifiers, int gridSize,
                 QImage *paintLayer = nullptr);
    void end();
    void clear();

    Action action() const { return m_selectionAction; }
    bool active() const { return m_selectionAction != Action::None; }
    const QRectF &rect() const { return m_selectionRect; }
    bool contains(const QPointF &point) const;
    bool mapToSurface(const QPointF &point, QPointF *surface) const;
    QPainterPath outline() const;

    // 只计算浮动图像，由调用方添加到文档并管理历史。失败时 bitmap 为空。
    FloatingImage copy(const QImage &source, const QPointF &point) const;
    FloatingImage clone() const;

private:
    QRect fillFromPoint(const QPointF &point, QImage &paintLayer);
    FloatingImage extractedImage(const QImage &bitmap, int hostFace) const;

    QVector<PerspectiveQuad> m_selectionFaces;
    QRectF m_selectionRect;
    QRectF m_selectionStartRect;
    QPointF m_selectionPressSurface;
    QPointF m_selectionFillOffset;
    Action m_selectionAction = Action::None;
    QImage m_selectionSampleSource;
    QImage m_selectionPaintBefore;
};
