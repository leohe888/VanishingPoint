#pragma once

#include "core/canvasdocument.h"
#include "core/planecreatetool.h"

#include <QPointF>
#include <QQuickPaintedItem>
#include <QString>

// 透视画布：负责视图变换（图像坐标 <-> 控件坐标）、输入路由与工具切换。
// 工具各自维护交互状态，几何、文档与渲染分别由 src/core 中的模块承担。
class VpCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    // 工具用短名暴露给 QML（"create"/"edit"/...），与工具栏按钮的 toolName 一致。
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)

public:
    enum class Tool { CreatePlane, EditPlane, Marquee, CloneStamp, Brush, Transform };

    explicit VpCanvas(QQuickItem *parent = nullptr);

    QString tool() const;
    void setTool(const QString &name);
    void setTool(Tool tool);

    void paint(QPainter *painter) override;

signals:
    void toolChanged();
    void statusMessage(const QString &text); // 提示栏文字

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPointF toImage(const QPointF &widgetPoint) const; // 控件坐标 -> 图像坐标
    void updateViewTransform();                        // 按控件尺寸计算“适应窗口”的缩放与居中
    void finishPlaneCreation();                        // 用 4 个角点生成平面并进入编辑工具
    void cancelInteraction();                          // 放弃进行中的交互（切换工具 / Esc）

    CanvasDocument m_doc;
    PlaneCreateTool m_createTool;
    Tool m_tool = Tool::CreatePlane;
    QPointF m_cursorPoint;   // 光标位置（图像坐标），离开画布时为空点
    qreal m_scale = 1.0;
    QPointF m_offset;
};
