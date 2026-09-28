#pragma once

#include "vpcontroller.h"
#include "core/floatingimagetransformtool.h"
#include "core/planecreatetool.h"
#include "core/planeedittool.h"

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

public:
    using Tool = VpController::Tool;

    explicit VpCanvas(QQuickItem *parent = nullptr);
    VpController *controller() { return &m_controller; }

    void paint(QPainter *painter) override;

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
    void updateCursorPoint(const QPointF &widgetPoint); // 记录光标并通知仿制源、预览跟随
    void finishPlaneCreation();
    bool extrudePlane(int sourcePlane, int edge); // 把拖动预览落成新的垂直平面
    void cancelInteraction();
    void deleteSelectedPlane();   // 删除当前选中的平面
    void reportCreateProgress();  // 状态栏提示创建进度
    bool cursorPreviewVisible() const; // 当前是否需要在光标处画预览
    void drawCloneMarker(QPainter *painter); // 仿制源的绿色十字指示

    // 浮动图像：按下/拖动/松开三处入口，命中测试与吸附动作。图像优先于工具——
    // 点到图像就拖动它，点到别处则把当前选中的图像烘焙进绘画层。
    bool beginFloatingImageInteraction(const QPointF &point);
    void updateFloatingImageInteraction(const QPointF &point, Qt::KeyboardModifiers modifiers);
    bool endFloatingImageInteraction();
    bool floatingImageAt(const QPointF &point, int *index, QPointF *grabOffset) const;
    void attachFloatingImageToPlane(int index, int planeIndex, const QPointF &point);
    bool moveSurfaceAttachedImage(int index, const QPointF &point);
    void bakeSelectedFloatingImage();
    void drawFloatingImageHandles(QPainter *painter); // 变换工具下的 8 个控制点

    // 选框：选区记在展开曲面上的一份面片快照里，因此可以跨越共享曲面的多个平面。
    // Alt 拖动把选区内容复制成浮动图像；Ctrl 拖动把光标处的内容克隆进选区。
    enum class SelectionAction { None, Create, Move, Fill };
    void clearSelection();
    bool pointToSelectionSurface(const QPointF &point, QPointF *surface) const;
    QPainterPath selectionPath() const; // 选区在画面上的轮廓（已按面片分段投影）
    void updateSelection(const QPointF &point, Qt::KeyboardModifiers modifiers);
    int copySelectionToFloatingImage(const QPointF &point); // Alt 拖动：复制选区内容
    int cloneSelectionToFloatingImage();                    // Ctrl 拖动：把克隆结果落成浮动图像
    void fillSelectionFromPoint(const QPointF &point);
    void drawSelectionOutline(QPainter *painter);

    VpController m_controller;
    VpDocument &m_doc; // 尚未迁移的平面、选区与浮动图像交互使用的模型引用

    PlaneCreateTool m_createTool;
    PlaneEditTool m_editTool;


    int m_editPlaneIndex = -1; // 正在编辑的平面下标。-1 同时表示“没有进行中的平面编辑”。

    PerspectivePlane m_extrudePreview;             // 拖出垂直平面时的预览几何
    bool m_extrudePreviewReady = false; // 预览几何是否可用


    QPointF m_cursorPoint;

    int m_antsPhase = 0; // 选中框虚线的相位，逐帧递增形成蚂蚁线

    FloatingImageTransformTool m_floatingImageTransform; // 进行中的图像移动/缩放/旋转
    int m_draggedFloatingImageIndex = -1;
    bool m_floatingImageChanged = false;

    QVector<PerspectiveQuad> m_selectionFaces;  // 选区所在曲面分组的几何快照（可跨多平面）
    QRectF m_selectionRect;           // 展开曲面坐标下的矩形选区
    QRectF m_selectionStartRect;      // 平移开始时的选区，避免逐帧累加误差
    QPointF m_selectionPressSurface;  // 按下点在展开曲面上的位置
    QPointF m_selectionFillOffset;    // Ctrl 克隆当前的取样偏移（展开曲面坐标）
    SelectionAction m_selectionAction = SelectionAction::None;
    QImage m_selectionSampleSource;   // Ctrl 克隆的取样源（按下时的文档合成）
    QImage m_selectionPaintBefore;    // Ctrl 克隆拖动前的绘画层，每帧据此重建预览

    qreal m_scale = 1.0;
    QPointF m_offset;
};
