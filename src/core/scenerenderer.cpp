#include "scenerenderer.h"

#include "vpdocument.h"
#include "floatingimageprojection.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QLineF>

// 控制点（创建平面的角点标记、平面编辑的角点与边缘中心点）统一的半边长，
// 单位为画布像素，绘制时再按视图缩放换算。
constexpr qreal HandleHalfSize = 4.0;

SceneRenderer::SceneRenderer(const VpDocument &doc)
    : m_doc(doc)
{
}

void SceneRenderer::renderContent(QPainter &painter, qreal viewScale) const
{
    painter.drawImage(QPointF(), m_doc.background());
    painter.drawImage(QPointF(), m_doc.paintLayer());
    for (const FloatingImage &image : m_doc.floatingImages())
        renderFloatingImage(painter, image, viewScale);
}

void SceneRenderer::renderGuides(QPainter &painter, qreal viewScale, const Guides &guides) const
{
    viewScale = qMax(viewScale, 1e-6);
    const auto &creationPoints = guides.creationPoints;
    const auto *extrudePreview = guides.extrudePreview;
    const bool editHandlesVisible = guides.editHandlesVisible;
    const qreal gridSize = guides.gridSize, antsPhase = guides.antsPhase;
    const QPointF cursorPoint = guides.cursorPoint;
    for (int i = 0; i < m_doc.planes().size(); ++i) {
        drawPlaneGuides(painter, m_doc.planes()[i].quad(), i == m_doc.selectedPlane(),
                        editHandlesVisible, gridSize, viewScale, guides.extrudeHandles, i);
    }
    if (extrudePreview)
        drawPlaneGuides(painter, extrudePreview->quad(), true, false, gridSize, viewScale, false);

    // 先画连线：已确定角点之间的边，以及连到光标的预览边。
    painter.save();
    painter.setPen(QPen(QColor("#4bc3ff"), 2.0 / viewScale));
    painter.setBrush(Qt::NoBrush);
    for (int i = 1; i < creationPoints.size(); ++i)
        painter.drawLine(creationPoints[i - 1], creationPoints[i]);
    // 橡皮筋预览：把光标与「下一个角点将要连到的那些角点」连起来。
    // 已点 1 个：连第 1 个；已点 2 个：连第 2 个；已点 3 个：连第 3 个，
    // 同时闭合回第 1 个——第四点落下后正好是四边形的两条收口边。
    const int last = creationPoints.size() - 1;
    const bool showCursor = last >= 0 && last <= 2 && !cursorPoint.isNull();
    if (showCursor) {
        painter.drawLine(creationPoints[last], cursorPoint);
        if (last == 2)
            painter.drawLine(cursorPoint, creationPoints[0]);
    }
    // 角点标记与平面控制点保持一致：白色方块 + 深色描边。
    // 光标处的待放置角点也用同一种方块，预览边两端看起来完全对称。
    painter.setPen(QPen(QColor("#0e526e"), 1.0 / viewScale));
    painter.setBrush(QColor("#f3f8fa"));
    const qreal handle = HandleHalfSize / viewScale;
    for (const QPointF &point : creationPoints)
        painter.drawRect(QRectF(point.x() - handle, point.y() - handle, handle * 2, handle * 2));
    if (showCursor)
        painter.drawRect(QRectF(cursorPoint.x() - handle, cursorPoint.y() - handle,
                                handle * 2, handle * 2));
    painter.restore();

    // 在屏幕坐标中描边，透视和缩放只改变轮廓，不改变线宽、虚线长度和速度。
    painter.save();
    const QTransform view = painter.worldTransform();
    painter.resetTransform();
    painter.setBrush(Qt::NoBrush);
    const int selectedFloatingImage = m_doc.selectedFloatingImage();
    if (selectedFloatingImage >= 0
        && selectedFloatingImage < m_doc.floatingImages().size()) {
        const QPainterPath outline = view.map(
            floatingImageOutline(m_doc.floatingImage(selectedFloatingImage)));
        QPen pen(Qt::white, 1);
        pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(pen);
        painter.drawPath(outline);
        pen.setColor(Qt::black);
        pen.setDashPattern({4, 4});
        pen.setDashOffset(antsPhase);
        painter.setPen(pen);
        painter.drawPath(outline);
    }
    painter.restore();
}

QPainterPath SceneRenderer::floatingImageOutline(const FloatingImage &image)
{
    return FloatingImageProjection::forImage(image)->canvasOutline();
}

// 渲染一张浮动图像：未吸附时直接绘制；已吸附时按几何快照分段投影。
void SceneRenderer::renderFloatingImage(QPainter &painter, const FloatingImage &image, qreal viewScale) const
{
    const auto projection = FloatingImageProjection::forImage(image);
    for (const ProjectedImagePatch &patch : projection->patches()) {
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // 在共同的画布坐标中裁剪，避免旋转后各面独立栅格化源裁剪路径
        // 时把共享边上的同一个像素同时排除。
        const QPainterPath projectedClip = patch.canvasClip;
        QPainterPathStroker seamTolerance;
        seamTolerance.setWidth(.04 / qMax(viewScale, 1e-6));
        seamTolerance.setJoinStyle(Qt::MiterJoin);
        painter.setClipPath(projectedClip.united(seamTolerance.createStroke(projectedClip)), Qt::IntersectClip);
        painter.setWorldTransform(patch.bitmapToCanvas.forward(), true);
        painter.drawImage(QPointF(0, 0), image.bitmap);
        painter.restore();
    }
}

// 绘制面片的编辑辅助元素：外框、内部网格，以及选中且处于编辑
// 工具时的控制点方块。
void SceneRenderer::drawPlaneGuides(QPainter &painter, const PerspectiveQuad &quad, bool selected,
                                    bool showHandles, qreal gridSize, qreal viewScale, bool extrudeHandles,
                                    int planeIndex) const
{
    painter.save();
    const qreal lineWidth = (selected ? 1.7 : 1.0) / viewScale;
    const QColor color = selected ? QColor(52, 195, 255, 230)
                                  : QColor(70, 155, 210, 160);
    painter.setPen(QPen(color, lineWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(quad.canvasPolygon());

    if (selected) {
        painter.save();
        QPainterPath planeClip;
        planeClip.addPolygon(quad.canvasPolygon());
        planeClip.closeSubpath();
        painter.setClipPath(planeClip, Qt::IntersectClip);
        painter.setPen(QPen(QColor(65, 182, 235, 145), 0.8 / viewScale));
        const qreal safeGridSize = qMax(gridSize, 4.0 / viewScale);
        // Derive cell counts from current projected edge lengths so resizing
        // a plane changes the number of cells instead of stretching them.
        const qreal horizontalLength =
            (QLineF(quad.canvasCorners()[0], quad.canvasCorners()[1]).length()
             + QLineF(quad.canvasCorners()[3], quad.canvasCorners()[2]).length()) * 0.5;
        const qreal verticalLength =
            (QLineF(quad.canvasCorners()[0], quad.canvasCorners()[3]).length()
             + QLineF(quad.canvasCorners()[1], quad.canvasCorners()[2]).length()) * 0.5;
        const int horizontalDivisions = int(qBound(1.0, horizontalLength / safeGridSize, 256.0));
        const int verticalDivisions = int(qBound(1.0, verticalLength / safeGridSize, 256.0));
        const PerspectiveTransform projection = quad.uvToCanvasTransform();
        for (int i = 1; i < horizontalDivisions; ++i) {
            const qreal t = qreal(i) / horizontalDivisions;
            QPointF start;
            QPointF end;
            if (projection.mapForward(QPointF(t, 0), &start)
                && projection.mapForward(QPointF(t, 1), &end))
                painter.drawLine(start, end);
        }
        for (int i = 1; i < verticalDivisions; ++i) {
            const qreal t = qreal(i) / verticalDivisions;
            QPointF start;
            QPointF end;
            if (projection.mapForward(QPointF(0, t), &start)
                && projection.mapForward(QPointF(1, t), &end))
                painter.drawLine(start, end);
        }
        painter.restore();
    }

    if (selected && showHandles) {
        const QVector<QPointF> points = quad.controlPoints();
        for (int i = 0; i < points.size(); ++i) {
            if (planeIndex >= 0 && !m_doc.planes()[planeIndex].controlPointEditable(i, extrudeHandles))
                continue;
            const qreal radius = HandleHalfSize / viewScale;
            painter.setPen(QPen(QColor("#0e526e"), 1.0 / viewScale));
            painter.setBrush(QColor("#f3f8fa"));
            painter.drawRect(QRectF(points[i].x() - radius, points[i].y() - radius,
                                    radius * 2, radius * 2));
        }
    }
    painter.restore();
}
