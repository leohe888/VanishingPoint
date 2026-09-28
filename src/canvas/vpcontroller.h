#pragma once

#include "tools/brushtool.h"
#include "tools/clonetool.h"
#include "core/vpdocument.h"

#include <QByteArray>
#include <QObject>

class QPainter;

class VpController final : public QObject
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
    enum Tool { CreatePlane, EditPlane, Marquee, CloneStamp, Brush, Transform, Hand, Zoom };
    Q_ENUM(Tool)

    explicit VpController(QObject *parent = nullptr) : QObject(parent) {}

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
    QString planeAngleLockReason() const;

    Q_INVOKABLE void pasteImage();
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
    void aboutToChangeTool();
    void toolChanged();
    void brushChanged();
    void cloneChanged();
    void gridSizeChanged();
    void planeAngleChanged();
    void statusMessage(const QString &text);
    void repaintRequested();
    void focusRequested();

private:
    const QImage &cloneSource();
    bool canSetSelectedPlaneAngle() const;

    VpDocument m_document;
    BrushTool m_brushTool;
    CloneTool m_cloneTool;
    QImage m_cloneSource;
    QByteArray m_cloneSourceKey;
    Tool m_tool = CreatePlane;
    int m_gridSize = 50;
};
