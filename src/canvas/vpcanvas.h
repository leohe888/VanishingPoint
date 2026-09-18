#pragma once

#include "core/brushtool.h"
#include "core/canvasdocument.h"
#include "core/planecreatetool.h"
#include "core/planeedittool.h"

#include <QColor>
#include <QPointF>
#include <QQuickPaintedItem>

class VpCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(int brushDiameter READ brushDiameter WRITE setBrushDiameter NOTIFY brushChanged)
    Q_PROPERTY(int brushHardness READ brushHardness WRITE setBrushHardness NOTIFY brushChanged)
    Q_PROPERTY(int brushOpacity READ brushOpacity WRITE setBrushOpacity NOTIFY brushChanged)
    Q_PROPERTY(QColor brushColor READ brushColor WRITE setBrushColor NOTIFY brushChanged)

public:
    enum Tool { CreatePlane, EditPlane, Marquee, CloneStamp, Brush, Transform };
    Q_ENUM(Tool)

    explicit VpCanvas(QQuickItem *parent = nullptr);

    Tool tool() const;
    void setTool(Tool tool);

    // —— 画笔选项 ——
    int brushDiameter() const;
    void setBrushDiameter(int value);
    int brushHardness() const;
    void setBrushHardness(int value);
    int brushOpacity() const;
    void setBrushOpacity(int value);
    QColor brushColor() const;
    void setBrushColor(const QColor &color);

    void paint(QPainter *painter) override;

signals:
    void toolChanged();
    void brushChanged();                      // 任一画笔选项变化
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
    bool extrudePlane(int sourcePlane, int edge); // 把拖动预览落成新的垂直平面
    void cancelInteraction();
    void deleteSelectedPlane();   // 删除当前选中的平面
    void reportCreateProgress();  // 状态栏提示创建进度
    bool cursorPreviewVisible() const; // 当前是否需要在光标处画预览

    CanvasDocument m_doc;

    PlaneCreateTool m_createTool;
    PlaneEditTool m_editTool;
    BrushTool m_brushTool;

    Tool m_tool = Tool::CreatePlane; // 当前工具


    int m_editPlaneIndex = -1; // 正在编辑的平面下标。-1 同时表示“没有进行中的平面编辑”。

    Plane m_extrudePreview;             // 拖出垂直平面时的预览几何
    bool m_extrudePreviewReady = false; // 预览几何是否可用

    QPointF m_cursorPoint;

    qreal m_scale = 1.0;
    QPointF m_offset;
};
