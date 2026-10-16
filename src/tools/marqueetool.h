#pragma once

#include "../core/floatingselection.h"
#include "../core/perspectiveplane.h"

#include <QPainterPath>
#include <QRectF>

// 选区使用展开曲面坐标和面片快照；不依赖画布、文档或撤销历史。
class MarqueeTool
{
public:
    enum class Action { None, Create, Move, Fill };
    int feather() const { return m_feather; }
    void setFeather(int value) { m_feather = qBound(0, value, 50); }
    int opacity() const { return m_opacity; }
    void setOpacity(int value) { m_opacity = qBound(0, value, 100); }
    int healMode() const { return m_healMode; }
    void setHealMode(int value) { m_healMode = qBound(0, value, 2); }

    bool beginCreate(const QVector<PerspectivePlane> &planes, const QPointF &point);
    bool beginMove(const QPointF &point);
    bool beginFill(const QPointF &point, const QImage &sampleSource, const QImage &paintBefore);
    // Fill 只更新取样偏移，独立预览层负责显示；保留旧参数以兼容调用方。
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
    quint64 outlineBuildCount() const { return m_outlineBuildCount; }

    // 只计算浮动选区，由调用方添加到文档并管理历史。失败时 bitmap 为空。
    FloatingSelection copy(const QImage &source, const QPointF &point) const;
    FloatingSelection copy(const QImage &source) const;
    FloatingSelection clone(qreal rasterScale = 1) const;

private:
    FloatingSelection extractedSelection(const QImage &bitmap, int hostFace) const;
    QRectF extractionRect() const;
    void featherPixels(QImage &bitmap, const QRectF &rect) const;

    QVector<PerspectiveQuad> m_selectionFaces;
    QVector<PerspectiveTransform> m_selectionMappings;
    QVector<QPainterPath> m_selectionFacePaths;
    mutable QPainterPath m_outline;
    mutable QRectF m_outlineRect;
    mutable bool m_outlineValid = false;
    mutable quint64 m_outlineBuildCount = 0;
    QRectF m_selectionRect;
    QRectF m_selectionStartRect;
    QPointF m_selectionPressSurface;
    QPointF m_selectionFillOffset;
    Action m_selectionAction = Action::None;
    QImage m_selectionSampleSource;
    int m_feather = 1;
    int m_opacity = 100;
    int m_healMode = 2;
};
