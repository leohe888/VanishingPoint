#pragma once

#include "core/perspectiveplane.h"

#include <QColor>
#include <QImage>
#include <QRect>

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

    QRect begin(QImage &layer, const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                const QPointF &point);
    QRect move(QImage &layer, const QPointF &point);
    void end() { m_drawing = false; }
    bool drawing() const { return m_drawing; }

    void renderPreview(QPainter &painter, const QVector<PerspectivePlane> &planes,
                       const QSize &canvasSize, const QPointF &point) const;

private:
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
