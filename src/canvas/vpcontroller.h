#pragma once

#include "../tools/floatingselectiontransformtool.h"
#include "../tools/marqueetool.h"
#include "../tools/planecreatetool.h"
#include "../tools/planeedittool.h"
#include "../tools/brushtool.h"
#include "../tools/clonetool.h"
#include "vpdocument.h"
#include "../core/tooltypes.h"

#include <QByteArray>
#include <QObject>
#include <QUrl>
#include <functional>
#include <utility>

class QPainter;

class VpController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY documentReplaced)
    Q_PROPERTY(QUrl savedImageUrl READ savedImageUrl NOTIFY savedImageUrlChanged)
    Q_PROPERTY(VpTools::Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QString toolHint READ toolHint NOTIFY toolChanged)
    Q_PROPERTY(QString interactionHint READ interactionHint NOTIFY interactionHintChanged)
    Q_PROPERTY(int availableTools READ availableTools NOTIFY toolAvailabilityChanged)
    Q_PROPERTY(int brushDiameter READ brushDiameter WRITE setBrushDiameter NOTIFY brushChanged)
    Q_PROPERTY(int brushHardness READ brushHardness WRITE setBrushHardness NOTIFY brushChanged)
    Q_PROPERTY(int brushOpacity READ brushOpacity WRITE setBrushOpacity NOTIFY brushChanged)
    Q_PROPERTY(QColor brushColor READ brushColor WRITE setBrushColor NOTIFY brushChanged)
    Q_PROPERTY(int cloneDiameter READ cloneDiameter WRITE setCloneDiameter NOTIFY cloneChanged)
    Q_PROPERTY(int cloneHardness READ cloneHardness WRITE setCloneHardness NOTIFY cloneChanged)
    Q_PROPERTY(int cloneOpacity READ cloneOpacity WRITE setCloneOpacity NOTIFY cloneChanged)
    Q_PROPERTY(bool cloneAligned READ cloneAligned WRITE setCloneAligned NOTIFY cloneChanged)
    Q_PROPERTY(int brushHealMode READ brushHealMode WRITE setBrushHealMode NOTIFY brushChanged)
    Q_PROPERTY(int cloneHealMode READ cloneHealMode WRITE setCloneHealMode NOTIFY cloneChanged)
    Q_PROPERTY(int marqueeFeather READ marqueeFeather WRITE setMarqueeFeather NOTIFY marqueeChanged)
    Q_PROPERTY(int marqueeOpacity READ marqueeOpacity WRITE setMarqueeOpacity NOTIFY marqueeChanged)
    Q_PROPERTY(int marqueeHealMode READ marqueeHealMode WRITE setMarqueeHealMode NOTIFY marqueeChanged)
    Q_PROPERTY(int marqueeMoveMode READ marqueeMoveMode WRITE setMarqueeMoveMode NOTIFY marqueeMoveModeChanged)
    Q_PROPERTY(bool marqueeMoveModeEnabled READ marqueeMoveModeEnabled NOTIFY marqueeMoveModeChanged)
    Q_PROPERTY(int gridSize READ gridSize WRITE setGridSize NOTIFY gridSizeChanged)
    Q_PROPERTY(qreal planeAngle READ planeAngle WRITE setPlaneAngle NOTIFY planeAngleChanged)
    Q_PROPERTY(bool planeAngleEditable READ planeAngleEditable NOTIFY planeAngleChanged)
    Q_PROPERTY(bool flippedHorizontally READ flippedHorizontally WRITE setFlippedHorizontally NOTIFY transformOptionsChanged)
    Q_PROPERTY(bool flippedVertically READ flippedVertically WRITE setFlippedVertically NOTIFY transformOptionsChanged)
    Q_PROPERTY(bool transformEditable READ transformEditable NOTIFY transformOptionsChanged)

public:
    explicit VpController(QObject *parent = nullptr);

    VpDocument &document() { return m_document; }
    const VpDocument &document() const { return m_document; }

    VpTools::Tool tool() const { return m_tool; }
    void setTool(VpTools::Tool tool);
    QString toolHint() const;
    QString interactionHint() const { return m_interactionHint; }
    int availableTools() const;
    bool isToolEnabled(VpTools::Tool tool) const;

    int brushDiameter() const { return m_brushTool.diameter(); }
    void setBrushDiameter(int value);
    int brushHardness() const { return m_brushTool.hardness(); }
    void setBrushHardness(int value);
    int brushOpacity() const { return m_brushTool.opacity(); }
    void setBrushOpacity(int value);
    QColor brushColor() const { return m_brushTool.color(); }
    void setBrushColor(const QColor &color);
    bool pickBrushColor(const QPointF &point);

    int cloneDiameter() const { return m_cloneTool.diameter(); }
    void setCloneDiameter(int value);
    int cloneHardness() const { return m_cloneTool.hardness(); }
    void setCloneHardness(int value);
    int cloneOpacity() const { return m_cloneTool.opacity(); }
    void setCloneOpacity(int value);
    bool cloneAligned() const { return m_cloneTool.aligned(); }
    void setCloneAligned(bool aligned);
    int brushHealMode() const { return m_brushTool.healMode(); }
    void setBrushHealMode(int mode);
    int cloneHealMode() const { return m_cloneTool.healMode(); }
    void setCloneHealMode(int mode);
    int marqueeMoveMode() const { return m_marqueeMoveMode; }
    bool marqueeMoveModeEnabled() const;
    void setMarqueeMoveMode(int mode);
    int marqueeFeather() const { return m_marqueeTool.feather(); }
    void setMarqueeFeather(int value);
    int marqueeOpacity() const { return m_marqueeTool.opacity(); }
    void setMarqueeOpacity(int value);
    int marqueeHealMode() const { return m_marqueeTool.healMode(); }
    void setMarqueeHealMode(int mode);

    int draggedPlaneControlPoint() const {
        return m_editPlaneIndex >= 0 && m_editPlaneIndex == m_document.selectedPlane()
            ? m_editTool.controlPointIndex() : -1;
    }
    int gridSize() const { return m_gridSize; }
    void setGridSize(int value);
    qreal planeAngle() const;
    void setPlaneAngle(qreal angle);
    bool planeAngleEditable() const;
    bool transformEditable() const;
    bool flippedHorizontally() const;
    bool flippedVertically() const;
    void setFlippedHorizontally(bool flipped);
    void setFlippedVertically(bool flipped);

    bool canUndo() const { return m_document.canUndo(); }
    bool canRedo() const { return m_document.canRedo(); }
    Q_INVOKABLE bool openImage(const QUrl &url);
    bool canSave() const { return m_document.hasLoadedImage(); }
    QUrl savedImageUrl() const { return m_savedImageUrl; }
    Q_INVOKABLE QUrl suggestedSaveUrl() const;
    Q_INVOKABLE bool saveImage(const QUrl &url);
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void pasteSelection();
    void setCopiedCanvasSelection(const QImage &image);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // 仅导出新增内容，不包含取样背景、网格或控制点。
    QImage resultImage() const;
    void setApplyHandler(std::function<bool(const QImage &)> handler) { m_applyHandler = std::move(handler); }
    Q_INVOKABLE bool applyChanges();
    void deleteSelection();
    Q_INVOKABLE void clearSelection();
    void cancelInteraction(bool clearMarquee = true);
    void pointerPress(const QPointF &point, qreal viewScale, Qt::KeyboardModifiers modifiers);
    void pointerMove(const QPointF &point, Qt::KeyboardModifiers modifiers);
    void pointerRelease(const QPointF &point, Qt::KeyboardModifiers modifiers);
    const QVector<QPointF> &creationPoints() const { return m_createTool.points(); }
    const PerspectivePlane *extrudePreview() const { return m_extrudePreviewReady ? &m_extrudePreview : nullptr; }
    QRectF selectionRect() const { return m_marqueeTool.rect(); }
    QPainterPath selectionOutline() const { return m_marqueeTool.outline(); }
    const FloatingSelection &selectionPreview() const { return m_selectionPreview; }
    void setSelectionPreviewDeviceScale(qreal scale) { m_selectionPreviewDeviceScale = scale; }
    quint64 sampleBuildCount() const { return m_sampleBuildCount; }
    bool cursorPreviewVisible() const;
    int paintTargetPlane(const QPointF &point) const;
    QString interactionCursorResource(const QPointF &point, qreal viewScale) const;
    qreal interactionCursorRotation(const QPointF &point, qreal viewScale) const;
    void notifyPlaneAngleChanged() { emit planeAngleChanged(); }
    void postStatus(const QString &text, bool error = false) { emit statusMessage(text, error); }

    bool brushDrawing() const { return m_brushTool.drawing(); }
    bool beginBrush(const QPointF &point);
    void moveBrush(const QPointF &point);
    void endBrush();
    void renderBrushPreview(QPainter &painter, const QPointF &point) const;
    QRectF brushPreviewBounds(const QPointF &point) const;

    bool hasCloneSource() const { return m_cloneTool.hasSource(); }
    bool cloneDrawing() const { return m_cloneTool.drawing(); }
    QPointF cloneMarker() const { return m_cloneTool.marker(); }
    bool pickCloneSource(const QPointF &point);
    bool beginClone(const QPointF &point);
    void moveClone(const QPointF &point);
    void endClone();
    void hoverClone(const QPointF &point);
    void renderClonePreview(QPainter &painter, const QPointF &point);
    QRectF clonePreviewBounds(const QPointF &point) const;

    // 切换工具时，已写入绘画层的笔触提交为一格历史。
    void finishActiveStrokes();

signals:
    void savedImageUrlChanged();
    void documentReplaced();
    void historyChanged();
    void toolChanged();
    void toolAvailabilityChanged();
    void brushChanged();
    void cloneChanged();
    void marqueeChanged();
    void marqueeMoveModeChanged();
    void gridSizeChanged();
    void planeAngleChanged();
    void transformOptionsChanged();
    void statusMessage(const QString &text, bool error = false);
    void statusMessageCleared();
    void interactionHintChanged();
    void repaintRequested();
    // Geometry/guides changed without changing sampled image pixels.
    void guidesRepaintRequested();
    void previewRepaintRequested();
    void paintRepaintRequested(const QRect &dirty);
    void focusRequested();

private:
    QUrl m_sourceImageUrl;
    QUrl m_savedImageUrl;
    void setInteractionHint(const QString &text);
    QString m_interactionHint;
    FloatingSelection m_copiedSelection; // 内部复制保留透视、变换及选区选项。
    bool m_preferInternalCopy = false;
    std::function<bool(const QImage &)> m_applyHandler;
    void refreshToolAvailability();
    void setSelectionFlipped(bool horizontal, bool flipped);
    void syncMarqueeOptions();
    void resetMarqueeMoveMode();
    bool reapplySourceFill();
    void finishPlaneCreation();
    bool extrudePlane(int sourcePlane, int edge);
    void deleteSelectedPlane();
    void reportCreateProgress();
    bool beginFloatingSelectionInteraction(const QPointF &point, qreal viewScale);
    void updateFloatingSelectionInteraction(const QPointF &point, Qt::KeyboardModifiers modifiers);
    bool endFloatingSelectionInteraction();
    bool floatingSelectionAt(const QPointF &point, int *index, QPointF *grabOffset) const;
    void attachFloatingSelectionToPlane(int index, int planeIndex, const QPointF &point);
    bool moveSurfaceAttachedSelection(int index, const QPointF &point);
    void bakeSelectedFloatingSelection();
    QImage selectionSampleImage(int excludedSelection = -1) const;
    FloatingSelection healedSelection(const FloatingSelection &selection, const QImage &target) const;
    int appendFloatingSelection(const FloatingSelection &image);
    void updateSelection(const QPointF &point, Qt::KeyboardModifiers modifiers);
    const QImage &cloneSource();

    VpDocument m_document;
    PlaneCreateTool m_createTool;
    PlaneEditTool m_editTool;
    MarqueeTool m_marqueeTool;
    int m_marqueeMoveMode = 0;
    bool m_marqueeSourceLocked = false;
    bool m_sourceModeFill = false;
    FloatingSelection m_sourceFillBase;
    QImage m_sourceFillTarget;
    QImage m_sourceFillPaintBefore;
    QRect m_sourceFillRect;
    qint64 m_sourceFillPaintKey = 0;
    FloatingSelection m_selectionPreview;
    qreal m_selectionPreviewDeviceScale = 1;

    bool m_ctrlPlaneCyclePending = false;
    bool m_startingCtrlPlaneDrag = false;
    QPointF m_ctrlPlanePressPoint;
    qreal m_ctrlPlaneViewScale = 1;
    Qt::KeyboardModifiers m_ctrlPlanePressModifiers = Qt::NoModifier;
    int m_editPlaneIndex = -1; // 正在编辑的平面下标。-1 同时表示“没有进行中的平面编辑”。

    PerspectivePlane m_extrudePreview;             // 拖出垂直平面时的预览几何
    bool m_extrudePreviewReady = false; // 预览几何是否可用

    FloatingSelectionTransformTool m_floatingSelectionTransform; // 进行中的图像移动/缩放/旋转
    int m_draggedFloatingSelectionIndex = -1;

    int m_strokePlaneIndex = -1;
    BrushTool m_brushTool;
    CloneTool m_cloneTool;
    QImage m_cloneSource;
    mutable QImage m_sampleImage;
    mutable QByteArray m_sampleKey;
    mutable quint64 m_sampleBuildCount = 0;
    VpTools::Tool m_tool = VpTools::CreatePlane;
    int m_availableTools = (1 << VpTools::CreatePlane) | (1 << VpTools::Hand) | (1 << VpTools::Zoom);
    int m_gridSize = 50;
};
