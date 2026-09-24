#pragma once

#include "core/brushtool.h"
#include "core/clonetool.h"
#include "core/vpdocument.h"

#include <QByteArray>

class QPainter;

// 编辑命令与文档事务的入口。坐标均为图像坐标，不依赖 QQuickItem。
// VpCanvas 接收 Qt 事件、换算坐标并请求重绘；控制器拥有文档和工具状态。
class VpController final
{
public:
    VpDocument &document() { return m_document; }
    const VpDocument &document() const { return m_document; }

    int brushDiameter() const { return m_brushTool.diameter(); }
    void setBrushDiameter(int value) { m_brushTool.setDiameter(value); }
    int brushHardness() const { return m_brushTool.hardness(); }
    void setBrushHardness(int value) { m_brushTool.setHardness(value); }
    int brushOpacity() const { return m_brushTool.opacity(); }
    void setBrushOpacity(int value) { m_brushTool.setOpacity(value); }
    QColor brushColor() const { return m_brushTool.color(); }
    void setBrushColor(const QColor &color) { m_brushTool.setColor(color); }

    int cloneDiameter() const { return m_cloneTool.diameter(); }
    void setCloneDiameter(int value) { m_cloneTool.setDiameter(value); }
    int cloneHardness() const { return m_cloneTool.hardness(); }
    void setCloneHardness(int value) { m_cloneTool.setHardness(value); }
    int cloneOpacity() const { return m_cloneTool.opacity(); }
    void setCloneOpacity(int value) { m_cloneTool.setOpacity(value); }
    bool cloneAligned() const { return m_cloneTool.aligned(); }
    void setCloneAligned(bool aligned) { m_cloneTool.setAligned(aligned); }

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

private:
    const QImage &cloneSource();

    VpDocument m_document;
    BrushTool m_brushTool;
    CloneTool m_cloneTool;
    QImage m_cloneSource;
    QByteArray m_cloneSourceKey;
};
