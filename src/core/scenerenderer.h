#pragma once

#include "perspectiveplane.h"

#include <QVector>
#include <QPainterPath>

class QPainter;
class VpDocument;
struct FloatingImage;

class SceneRenderer
{
public:
    explicit SceneRenderer(const VpDocument &doc);

    struct Guides {
        QVector<QPointF> creationPoints;
        const PerspectivePlane *extrudePreview = nullptr;
        bool editHandlesVisible = false;
        qreal antsPhase = 0;
        qreal gridSize = 50;
        QPointF cursorPoint;
    };
    void renderContent(QPainter &painter, qreal viewScale = 1.0) const;
    void renderGuides(QPainter &painter, qreal viewScale, const Guides &guides) const;

    // 各投影片段的并集外轮廓，不包含平面之间的内部接缝。
    static QPainterPath floatingImageOutline(const FloatingImage &image);

    // 把一张浮动图像单独绘制到指定 QPainter（画布坐标、视图缩放为 1）。
    // 正常渲染与「烘焙进绘画层」共用这条路径，两处看到的像素完全一致。
    void renderFloatingImage(QPainter &painter, const FloatingImage &image, qreal viewScale = 1.0) const;

private:
    // 绘制面片的编辑辅助元素：外框、内部网格，以及选中且处于编辑
    // 工具时的控制点方块。
    void drawPlaneGuides(QPainter &painter, const PerspectiveQuad &quad, bool selected,
                         bool showHandles, qreal gridSize, qreal viewScale,
                         int planeIndex = -1) const;

    const VpDocument &m_doc;   // 被渲染的文档（只读）
};
