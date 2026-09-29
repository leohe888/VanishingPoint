#pragma once

#include "tools/floatingimagetransformtool.h"
#include "tools/marqueetool.h"
#include "tools/planecreatetool.h"
#include "tools/planeedittool.h"
#include "tools/brushtool.h"
#include "tools/clonetool.h"
#include "core/vpdocument.h"

#include <QByteArray>
#include <QObject>
#include <QUrl>

class QPainter;

class VpController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
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

public:
    enum Tool { CreatePlane, EditPlane, Marquee, CloneStamp, Brush, Transform, Hand, Zoom };
    Q_ENUM(Tool)

    explicit VpController(QObject *parent = nullptr);

    VpDocument &document() { return m_document; }
    const VpDocument &document() const { return m_document; }

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);

    int brushDiameter() const { return m_brushTool.diameter(); }
    void setBrushDiameter(int value);
    int brushHardness() const { return m_brushTool.hardness(); }
    void setBrushHardness(int value);
    int brushOpacity() const { return m_brushTool.opacity(); }
    void setBrushOpacity(int value);
    QColor brushColor() const { return m_brushTool.color(); }
    void setBrushColor(const QColor &color);

    int cloneDiameter() const { return m_cloneTool.diameter(); }
    void setCloneDiameter(int value);
    int cloneHardness() const { return m_cloneTool.hardness(); }
    void setCloneHardness(int value);
    int cloneOpacity() const { return m_cloneTool.opacity(); }
    void setCloneOpacity(int value);
    bool cloneAligned() const { return m_cloneTool.aligned(); }
    void setCloneAligned(bool aligned);

    int gridSize() const { return m_gridSize; }
    void setGridSize(int value);
    qreal planeAngle() const;
    void setPlaneAngle(qreal angle);
    bool planeAngleEditable() const;

    Q_INVOKABLE bool openImage(const QUrl &url);
    bool canUndo() const { return m_document.canUndo(); }
    bool canRedo() const { return m_document.canRedo(); }
    Q_INVOKABLE void pasteImage();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    void deleteSelection();
    void cancelInteraction();
    void pointerPress(const QPointF &point, qreal viewScale, Qt::KeyboardModifiers modifiers);
    void pointerMove(const QPointF &point, Qt::KeyboardModifiers modifiers);
    void pointerRelease(const QPointF &point, Qt::KeyboardModifiers modifiers);
    const QVector<QPointF> &creationPoints() const { return m_createTool.points(); }
    const PerspectivePlane *extrudePreview() const { return m_extrudePreviewReady ? &m_extrudePreview : nullptr; }
    QRectF selectionRect() const { return m_marqueeTool.rect(); }
    QPainterPath selectionOutline() const { return m_marqueeTool.outline(); }
    bool cursorPreviewVisible() const;
    void notifyPlaneAngleChanged() { emit planeAngleChanged(); }
    void postStatus(const QString &text) { emit statusMessage(text); }

    bool brushDrawing() const { return m_brushTool.drawing(); }
    bool beginBrush(const QPointF &point);
    void moveBrush(const QPointF &point);
    void endBrush();
    void renderBrushPreview(QPainter &painter, const QPointF &point) const;

    bool hasCloneSource() const { return m_cloneTool.hasSource(); }
    bool cloneDrawing() const { return m_cloneTool.drawing(); }
    QPointF cloneMarker() const { return m_cloneTool.marker(); }
    bool pickCloneSource(const QPointF &point);
    bool beginClone(const QPointF &point);
    void moveClone(const QPointF &point);
    void endClone();
    void hoverClone(const QPointF &point);
    void renderClonePreview(QPainter &painter, const QPointF &point);

    // 切换工具时，已写入绘画层的笔触提交为一格历史。
    void finishActiveStrokes();

signals:
    void documentReplaced();
    void historyChanged();
    void toolChanged();
    void brushChanged();
    void cloneChanged();
    void gridSizeChanged();
    void planeAngleChanged();
    void statusMessage(const QString &text);
    void repaintRequested();
    void focusRequested();

private:
    void finishPlaneCreation();
    bool extrudePlane(int sourcePlane, int edge);
    void deleteSelectedPlane();
    void reportCreateProgress();
    bool beginFloatingImageInteraction(const QPointF &point, qreal viewScale);
    void updateFloatingImageInteraction(const QPointF &point, Qt::KeyboardModifiers modifiers);
    bool endFloatingImageInteraction();
    bool floatingImageAt(const QPointF &point, int *index, QPointF *grabOffset) const;
    void attachFloatingImageToPlane(int index, int planeIndex, const QPointF &point);
    bool moveSurfaceAttachedImage(int index, const QPointF &point);
    void bakeSelectedFloatingImage();
    QImage selectionSampleImage() const;
    int appendSelectionImage(const FloatingImage &image);
    void updateSelection(const QPointF &point, Qt::KeyboardModifiers modifiers);
    const QImage &cloneSource();

    VpDocument m_document;
    PlaneCreateTool m_createTool;
    PlaneEditTool m_editTool;
    MarqueeTool m_marqueeTool;

    int m_editPlaneIndex = -1; // 正在编辑的平面下标。-1 同时表示“没有进行中的平面编辑”。

    PerspectivePlane m_extrudePreview;             // 拖出垂直平面时的预览几何
    bool m_extrudePreviewReady = false; // 预览几何是否可用

    FloatingImageTransformTool m_floatingImageTransform; // 进行中的图像移动/缩放/旋转
    int m_draggedFloatingImageIndex = -1;

    BrushTool m_brushTool;
    CloneTool m_cloneTool;
    QImage m_cloneSource;
    QByteArray m_cloneSourceKey;
    Tool m_tool = CreatePlane;
    int m_gridSize = 50;
};
