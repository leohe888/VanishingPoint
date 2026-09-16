#pragma once

#include "core/canvasdocument.h"
#include "core/planecreatetool.h"
#include "core/planeedittool.h"

#include <QPointF>
#include <QQuickPaintedItem>

class VpCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)

public:
    enum Tool { CreatePlane, EditPlane, Marquee, CloneStamp, Brush, Transform };
    Q_ENUM(Tool)

    explicit VpCanvas(QQuickItem *parent = nullptr);

    Tool tool() const;
    void setTool(Tool tool);

    void paint(QPainter *painter) override;

signals:
    void toolChanged();
    void statusMessage(const QString &text); // 提示栏文字

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPointF widgetToImage(const QPointF &widgetPoint) const;
    void updateViewTransform();
    void finishPlaneCreation();
    void cancelInteraction();
    void reportCreateProgress();  // 状态栏提示创建进度

    CanvasDocument m_doc;

    PlaneCreateTool m_createTool;
    PlaneEditTool m_editTool;

    Tool m_tool = Tool::CreatePlane; // 当前工具


    int m_editPlaneIndex = -1; // 正在编辑的平面下标。-1 同时表示“没有进行中的平面编辑”。

    QPointF m_cursorPoint;

    qreal m_scale = 1.0;
    QPointF m_offset;
};
