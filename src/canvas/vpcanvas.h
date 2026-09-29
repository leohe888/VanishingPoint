#pragma once

#include "vpcontroller.h"

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QQuickPaintedItem>

class VpCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(VpController *controller READ controller CONSTANT)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(qreal horizontalSize READ horizontalSize NOTIFY viewChanged)
    Q_PROPERTY(qreal verticalSize READ verticalSize NOTIFY viewChanged)
    Q_PROPERTY(qreal horizontalPosition READ horizontalPosition NOTIFY viewChanged)
    Q_PROPERTY(qreal verticalPosition READ verticalPosition NOTIFY viewChanged)

public:
    using Tool = VpController::Tool;

    explicit VpCanvas(QQuickItem *parent = nullptr);
    VpController *controller() { return &m_controller; }

    void paint(QPainter *painter) override;
    qreal zoom() const { return m_scale; }
    qreal horizontalSize() const;
    qreal verticalSize() const;
    qreal horizontalPosition() const;
    qreal verticalPosition() const;
    Q_INVOKABLE void setZoom(qreal scale);
    Q_INVOKABLE void zoomStep(bool out);
    Q_INVOKABLE void fitView(bool fill = false);
    Q_INVOKABLE void scrollTo(qreal horizontal, qreal vertical);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

signals:
    void viewChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QPointF widgetToImage(const QPointF &widgetPoint) const;
    void updateViewTransform();
    void zoomAt(qreal scale, const QPointF &anchor);
    void stepAt(bool out, const QPointF &anchor);
    void updateNavigationCursor(bool alt = false);
    void updateCursorPoint(const QPointF &widgetPoint); // 记录光标并通知仿制源、预览跟随
    const VpDocument &document() const { return m_controller.document(); }
    void drawCloneMarker(QPainter *painter); // 仿制源的绿色十字指示

    void drawFloatingImageHandles(QPainter *painter); // 变换工具下的 8 个控制点

    void drawSelectionOutline(QPainter *painter);

    VpController m_controller;
    QPointF m_cursorPoint;

    int m_antsPhase = 0; // 选中框虚线的相位，逐帧递增形成蚂蚁线

    qreal m_scale = 1.0;
    QPointF m_offset;
    bool m_fitView = true;
    bool m_fillView = false;
    bool m_panning = false;
    QPointF m_panPoint;
};
