#pragma once

#include "perspectiveplane.h"

#include <QVector>
#include <QPainterPath>

class QPainter;
class VpDocument;
struct FloatingSelection;

class SceneRenderer
{
public:
    explicit SceneRenderer(const VpDocument &doc);

    struct Guides {
        QVector<QPointF> creationPoints;
        const PerspectivePlane *extrudePreview = nullptr;
        bool editHandlesVisible = false;
        bool extrudeHandles = false;
        int draggedControlPoint = -1;
        int highlightedPlane = -1;
        qreal gridSize = 50;
        QPointF cursorPoint;
    };
    void renderContent(QPainter &painter, qreal viewScale = 1.0) const;
    void renderGuides(QPainter &painter, qreal viewScale, const Guides &guides) const;
    void renderSelectionGuides(QPainter &painter) const;

    // 各投影片段的并集外轮廓，不包含平面之间的内部接缝。
    static QPainterPath floatingSelectionOutline(const FloatingSelection &image);

    // 把一张浮动选区单独绘制到指定 QPainter（画布坐标、视图缩放为 1）。
    // 正常渲染与「烘焙进绘画层」共用这条路径，两处看到的像素完全一致。
    static void renderFloatingSelection(QPainter &painter, const FloatingSelection &image, qreal viewScale = 1.0);

private:
    // 绘制面片的编辑辅助元素：外框、内部网格，以及选中且处于编辑
    // 工具时的控制点方块。
    void drawPlaneGuides(QPainter &painter, const PerspectiveQuad &quad, bool selected,
                         bool showHandles, qreal gridSize, qreal viewScale, bool extrudeHandles,
                         int planeIndex = -1, int draggedControlPoint = -1, bool highlighted = false) const;

    const VpDocument &m_doc;   // 被渲染的文档（只读）
};
