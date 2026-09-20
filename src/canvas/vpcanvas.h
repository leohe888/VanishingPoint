#pragma once

#include "core/brushtool.h"
#include "core/canvasdocument.h"
#include "core/clonetool.h"
#include "core/imagetransformtool.h"
#include "core/planecreatetool.h"
#include "core/planeedittool.h"

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QQuickPaintedItem>

class VpCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(int brushDiameter READ brushDiameter WRITE setBrushDiameter NOTIFY brushChanged)
    Q_PROPERTY(int brushHardness READ brushHardness WRITE setBrushHardness NOTIFY brushChanged)
    Q_PROPERTY(int brushOpacity READ brushOpacity WRITE setBrushOpacity NOTIFY brushChanged)
    Q_PROPERTY(QColor brushColor READ brushColor WRITE setBrushColor NOTIFY brushChanged)
    Q_PROPERTY(int cloneDiameter READ cloneDiameter WRITE setCloneDiameter NOTIFY cloneChanged)
    Q_PROPERTY(int cloneHardness READ cloneHardness WRITE setCloneHardness NOTIFY cloneChanged)
    Q_PROPERTY(int cloneOpacity READ cloneOpacity WRITE setCloneOpacity NOTIFY cloneChanged)
    Q_PROPERTY(bool cloneAligned READ cloneAligned WRITE setCloneAligned NOTIFY cloneChanged)
    Q_PROPERTY(int gridSize READ gridSize WRITE setGridSize NOTIFY gridSizeChanged)
    Q_PROPERTY(qreal planeAngle READ planeAngle WRITE setPlaneAngle NOTIFY planeAngleChanged)
    Q_PROPERTY(bool planeAngleEditable READ planeAngleEditable NOTIFY planeAngleChanged)
    Q_PROPERTY(QString planeAngleLockReason READ planeAngleLockReason NOTIFY planeAngleChanged)

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

    // —— 图章选项 ——
    int cloneDiameter() const;
    void setCloneDiameter(int value);
    int cloneHardness() const;
    void setCloneHardness(int value);
    int cloneOpacity() const;
    void setCloneOpacity(int value);
    bool cloneAligned() const;
    void setCloneAligned(bool aligned);

    // —— 平面选项 ——
    int gridSize() const;
    void setGridSize(int value);
    qreal planeAngle() const;             // 选中平面与父平面的夹角（度）；无选中时为 90
    void setPlaneAngle(qreal angle);      // 只有子平面、且它没有被更下一级锁定时才生效
    bool planeAngleEditable() const;
    QString planeAngleLockReason() const; // 不可调整的原因；可调整时为空串

    Q_INVOKABLE void pasteImage(); // 把剪贴板里的位图粘贴成一张浮动图像

    void paint(QPainter *painter) override;

signals:
    void toolChanged();
    void brushChanged();                      // 任一画笔选项变化
    void cloneChanged();                      // 任一图章选项变化
    void gridSizeChanged();                   // 网格边长变化
    void planeAngleChanged();                 // 夹角数值或它的可编辑性变化
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
    void updateCursorPoint(const QPointF &widgetPoint); // 记录光标并通知仿制源、预览跟随
    void finishPlaneCreation();
    bool extrudePlane(int sourcePlane, int edge); // 把拖动预览落成新的垂直平面
    bool canSetSelectedPlaneAngle() const;        // 选中平面是否是「可调夹角的子平面」
    void cancelInteraction();
    void deleteSelectedPlane();   // 删除当前选中的平面
    void reportCreateProgress();  // 状态栏提示创建进度
    bool cursorPreviewVisible() const; // 当前是否需要在光标处画预览
    const QImage &cloneSource();  // 仿制取样的内容（按内容缓存）
    void drawCloneMarker(QPainter *painter); // 仿制源的绿色十字指示

    // 浮动图像：按下/拖动/松开三处入口，命中测试与吸附动作。图像优先于工具——
    // 点到图像就拖动它，点到别处则把当前选中的图像烘焙进绘画层。
    bool beginImageInteraction(const QPointF &point);
    void updateImageInteraction(const QPointF &point, Qt::KeyboardModifiers modifiers);
    bool endImageInteraction();
    bool floatingImageAt(const QPointF &point, int *index, QPointF *grabOffset) const;
    void attachImageToPlane(int index, int planeIndex, const QPointF &point);
    bool moveAttachedImage(int index, const QPointF &point);
    void bakeSelectedImage();
    void drawImageHandles(QPainter *painter); // 变换工具下的 8 个控制点

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

    CanvasDocument m_doc;

    PlaneCreateTool m_createTool;
    PlaneEditTool m_editTool;
    BrushTool m_brushTool;
    CloneTool m_cloneTool;

    Tool m_tool = Tool::CreatePlane; // 当前工具

    int m_editPlaneIndex = -1; // 正在编辑的平面下标。-1 同时表示“没有进行中的平面编辑”。

    Plane m_extrudePreview;             // 拖出垂直平面时的预览几何
    bool m_extrudePreviewReady = false; // 预览几何是否可用

    int m_gridSize = 50; // 平面网格边长（图像像素），单位与展开曲面坐标一致

    QPointF m_cursorPoint;

    QImage m_cloneSource;        // 仿制取样的内容快照
    QByteArray m_cloneSourceKey; // 快照对应的内容键

    int m_antsPhase = 0; // 选中框虚线的相位，逐帧递增形成蚂蚁线

    ImageTransformTool m_imageTool;  // 进行中的图像缩放/旋转
    int m_draggingImage = -1;        // 正在拖动的浮动图像下标
    bool m_imageChanged = false;     // 本次拖动是否真的改过图像几何

    QVector<Facet> m_selectionFaces;  // 选区所在曲面分组的几何快照（可跨多平面）
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
