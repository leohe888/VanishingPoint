#pragma once

#include "../core/perspectiveplane.h"
#include "../core/healingblend.h"

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QRect>
#include <functional>

class QPainter;

class BrushTool
{
public:
    void setDiameter(int value) { m_diameter = qBound(1, value, 500); }
    void setHardness(int value) { m_hardness = qBound(0, value, 100); }
    void setOpacity(int value) { m_opacity = qBound(1, value, 100); }
    void setColor(const QColor &color) { m_brushColor = color; }
    int diameter() const { return m_diameter; }
    int hardness() const { return m_hardness; }
    int opacity() const { return m_opacity; }
    QColor color() const { return m_brushColor; }
    int healMode() const { return m_healMode; }
    void setHealMode(int mode) { m_healMode = qBound(0, mode, 2); }

    QRect begin(QImage &layer, const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                const QPointF &point, const std::function<void(const QRect &)> &prepare = {},
                const QImage &healReference = {});
    QRect move(QImage &layer, const QPointF &point, const std::function<void(const QRect &)> &prepare = {});
    // 结束一笔时才替换为修复结果；不传 layer 时仅清理交互状态。
    QRect end(QImage *layer = nullptr);
    bool drawing() const { return m_drawing; }
    void setSelectionClip(const QPainterPath &clip) { m_selectionClip = clip; }

    void renderPreview(QPainter &painter, const QVector<PerspectivePlane> &planes,
                       const QSize &canvasSize, const QPointF &point) const;
    QRectF previewBounds(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                         const QPointF &point) const;

private:
    void paintDab(QPainter &painter, const QTransform &surfaceToCanvas, const QPointF &position) const;
    static bool resolveTarget(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                              const QPointF &point, PerspectiveTransform *mapping, QPointF *position);

    QRect applyDab(QPainter &painter, const PerspectiveTransform &mapping, const QPointF &position,
                   const std::function<void(const QRect &)> &prepare = {},
                   const QImage &healReference = {}) const;
    QRect drawStrokeTo(QImage &layer, const QPointF &position, const std::function<void(const QRect &)> &prepare);

    PerspectiveTransform m_mapping;
    QPointF m_lastPosition;          // 展开平面坐标；直径和补点间距也使用该坐标的单位
    QColor m_brushColor = QColor("#e85d4a");
    int m_diameter = 42;
    int m_hardness = 75;
    int m_opacity = 100;
    bool m_drawing = false;
    QPainterPath m_selectionClip;
    int m_healMode = HealingBlend::Off;
    QImage m_healReference;
    struct Dab {
        QPointF position;
        int diameter, hardness, opacity;
        QColor color;
    };
    QVector<Dab> m_healDabs;
    QImage m_beforeHealStroke;
    QRect m_healDirty;
    bool m_finalizingHeal = false;
};
