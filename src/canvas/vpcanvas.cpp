#include "vpcanvas.h"

#include "core/floatingimageprojection.h"
#include "core/scenerenderer.h"

#include <QCursor>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQmlEngine>
#include <QTimer>
#include <QGuiApplication>
#include <cmath>

namespace {
constexpr qreal ViewMargin = 16.0; // 图像与画布边缘的留白
}

VpCanvas::VpCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent), m_doc(m_controller.document())
{
    setAcceptHoverEvents(true);                 // 接受鼠标悬停事件
    setAcceptedMouseButtons(Qt::LeftButton);    // 接受鼠标左键
    setActiveFocusOnTab(true);                  // 允许通过 Tab 键获得焦点
    setAntialiasing(true);                      // 开启抗锯齿
    setCursor(Qt::ArrowCursor);                 // 光标统一用箭头

    // 切换工具后，更新画布的鼠标光标
    connect(&m_controller, &VpController::toolChanged, this, [this] {
        updateNavigationCursor(QGuiApplication::keyboardModifiers() & Qt::AltModifier);
    });

    // 告诉 QML 引擎：m_controller 由 C++ 管理，QML 垃圾回收不能删除它
    QQmlEngine::setObjectOwnership(&m_controller, QQmlEngine::CppOwnership);

    // 切换工具前，取消当前交互
    connect(&m_controller, &VpController::aboutToChangeTool,
            this, &VpCanvas::cancelInteraction);
    connect(&m_controller, &VpController::aboutToExecuteCommand, this, &VpCanvas::cancelInteraction);
    connect(&m_controller, &VpController::repaintRequested, this, [this] { update(); });
    // 控制器请求键盘焦点，画布主动获取焦点
    connect(&m_controller, &VpController::focusRequested, this, [this] { forceActiveFocus(); });

    constexpr auto defaultBackgroundPath = R"(C:\Users\yixin\Pictures\3.jpg)";
    m_doc.loadImage(QString::fromUtf8(defaultBackgroundPath));  // 启动时加载默认背景

    // 选中浮动图像或存在选区时让虚线跑起来；都没有就什么都不做，避免空转重绘。
    auto *antsTimer = new QTimer(this);
    antsTimer->setInterval(80);
    connect(antsTimer, &QTimer::timeout, this, [this] {
        if (!isVisible()
            || (m_doc.selectedFloatingImage() < 0 && m_marqueeTool.rect().isEmpty()))
            return;
        m_antsPhase = (m_antsPhase + 1) % 8;
        update();
    });
    antsTimer->start();

    // 选中消失（烘焙进绘画层、被删除）后变换工具已无从操作，自动退回编辑平面工具；
    // setTool 内部会放弃进行中的交互，toolChanged 负责把 QML 工具栏一起带回去。
    connect(&m_doc, &VpDocument::imageSelectionChanged, this, [this](bool selected) {
        if (!selected && m_controller.tool() == Tool::Transform)
            m_controller.setTool(Tool::EditPlane);
        update();
    });

    updateViewTransform();
}

void VpCanvas::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), QColor("#4D4D4D"));
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter->translate(m_offset);
    painter->scale(m_scale, m_scale);
    SceneRenderer(m_doc).render(*painter, m_scale, /*showGuides*/ true,
                                m_createTool.points(),
                                m_extrudePreviewReady ? &m_extrudePreview : nullptr,
                                /*editHandlesVisible*/ m_controller.tool() == Tool::EditPlane,
                                /*hoveredPlane*/ -1, m_antsPhase, /*drawContent*/ true,
                                /*gridSize*/ m_controller.gridSize(),
                                m_controller.tool() == Tool::CreatePlane ? m_cursorPoint : QPointF());
    // 光标离开画布时不画预览：空点 (0,0) 同时也是合法的图像坐标
    const bool cursorOnCanvas = !m_cursorPoint.isNull();
    if (cursorOnCanvas && m_controller.tool() == Tool::Brush)
        m_controller.renderBrushPreview(*painter, m_cursorPoint);
    if (m_controller.tool() == Tool::CloneStamp) {
        if (cursorOnCanvas)
            m_controller.renderClonePreview(*painter, m_cursorPoint);
        drawCloneMarker(painter);
    }
    drawFloatingImageHandles(painter);
    drawSelectionOutline(painter);
    painter->restore();
}

// 仿制源用绿色十字标出，线宽与臂长都按视图缩放换算，屏幕上尺寸恒定
void VpCanvas::drawCloneMarker(QPainter *painter)
{
    if (!m_controller.hasCloneSource())
        return;
    const QPointF marker = m_controller.cloneMarker();
    const qreal arm = 7.0 / m_scale;
    painter->save();
    painter->setPen(QPen(QColor("#00e676"), 1.0 / m_scale));
    painter->drawLine(QPointF(marker.x() - arm, marker.y()), QPointF(marker.x() + arm, marker.y()));
    painter->drawLine(QPointF(marker.x(), marker.y() - arm), QPointF(marker.x(), marker.y() + arm));
    painter->restore();
}

void VpCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);    // 必须先调用基类实现
    if (newGeometry.size() != oldGeometry.size())
        updateViewTransform();
}

void VpCanvas::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(); // 获得键盘焦点
    event->accept();    // 标记事件已处理
    if (m_controller.tool() == Tool::Hand) {
        m_fitView = false;
        m_panning = true;
        m_panPoint = event->position();
        updateNavigationCursor();
        return;
    }
    if (m_controller.tool() == Tool::Zoom) {
        stepAt(event->modifiers() & Qt::AltModifier, event->position());
        return;
    }
    const QPointF point = widgetToImage(event->position());
    if (beginFloatingImageInteraction(point)) // 浮动图像浮在最上层，先于任何工具处理
        return;
    switch (m_controller.tool()) {
    case Tool::EditPlane: {
        const qreal tolerance = qMax(8.0 / qMax(m_scale, 1e-6), 4.0);
        int planeIndex = -1;
        int handle = -1;
        // 先检测控制点。后创建的平面在上层，先被检查
        for (int i = m_doc.planes().size() - 1; i >= 0; --i) {
            const int candidateHandle = m_doc.planes()[i].quad().controlPointIndexAt(point, tolerance);
            if (candidateHandle >= 0 && m_doc.planes()[i].controlPointEditable(candidateHandle)) {
                planeIndex = i;
                handle = candidateHandle;
                break;
            }
        }
        // 如果没点到控制点，再检测平面本体
        if (planeIndex < 0)
            planeIndex = topmostPlaneIndexAt(m_doc.planes(), point);

        // 什么都没点到，则取消选择
        if (planeIndex < 0) {
            m_doc.setSelectedPlane(-1);
            m_controller.notifyPlaneAngleChanged(); // 没有选中平面，夹角回到不可调
            update();
            return;
        }

        // 选中平面
        m_doc.setSelectedPlane(planeIndex);
        m_controller.notifyPlaneAngleChanged();
        // 与相邻平面共边的平面不能整体平移，否则共用边会被撕开
        if (handle < 0 && m_doc.isPlaneLinked(planeIndex)) {
            m_controller.postStatus(tr("该平面已与相邻平面共边，不能整体移动。"));
            update();
            return;
        }
        const PerspectivePlane &plane = m_doc.planes()[planeIndex];
        const int edge = handle >= 4 ? handle - 4 : -1;
        // Ctrl + 拖动边中点：从这条边拖出一个与之垂直的新平面
        const bool extrude = edge >= 0 && (event->modifiers() & Qt::ControlModifier);
        // Alt + 拖动共用边对面的边中点：绕共用边旋转这个子平面，即改它与父平面的夹角
        const bool rotate = (event->modifiers() & Qt::AltModifier) && plane.parentPlaneIndex() >= 0
                            && handle == 4 + 2;
        if (extrude && plane.isEdgeLocked(edge))
            return;
        if (rotate && !m_controller.planeAngleEditable()) {
            m_controller.postStatus(m_controller.planeAngleLockReason());
            return;
        }
        m_editPlaneIndex = planeIndex;
        m_extrudePreviewReady = false;
        m_editTool.begin(plane, point, handle, edge, extrude, m_doc.background().size(),
                         rotate, rotate ? 0 : -1);
        m_doc.beginEdit();
        if (extrude)
            m_controller.postStatus(tr("拖动以拉出垂直平面，松开完成。"));
        else if (rotate)
            m_controller.postStatus(tr("拖动以调整与父平面的夹角，松开完成。"));
        update();
        return;
    }
    case Tool::CreatePlane:
        m_createTool.addPoint(point);
        if (m_createTool.finished())
            finishPlaneCreation();
        else
            reportCreateProgress();
        update();
        return;
    case Tool::Brush: {
        if (m_controller.beginBrush(point))
            update();
        return;
    }
    case Tool::CloneStamp: {
        // Alt+单击只取源点，不落笔
        if (event->modifiers() & Qt::AltModifier) {
            const bool picked = m_controller.pickCloneSource(point);
            m_controller.postStatus(picked ? tr("已设置仿制源，按住 Alt 可重新取样")
                                      : tr("此处无法作为仿制源。"));
            update();
            return;
        }
        if (!m_controller.hasCloneSource()) {
            m_controller.postStatus(tr("请先按住 Alt 单击，设置仿制源。"));
            return;
        }
        if (m_controller.beginClone(point))
            update();
        return;
    }
    case Tool::Marquee: {
        QPointF surface;
        const bool insideSelection = m_marqueeTool.contains(point)
                                     && m_marqueeTool.mapToSurface(point, &surface);
        // Alt 拖动：复制内容为浮动图像，并立即接续图像移动。
        if (insideSelection && (event->modifiers() & Qt::AltModifier)) {
            const int index = appendSelectionImage(m_marqueeTool.copy(selectionSampleImage(), point));
            if (index >= 0) {
                m_draggedFloatingImageIndex = index;
                m_doc.beginEdit();
                m_floatingImageTransform.beginMove(
                    m_doc.floatingImage(index), surface - m_marqueeTool.rect().topLeft());
                m_marqueeTool.clear();
                m_controller.postStatus(tr("已复制选区内容为浮动图像。"));
            }
            update();
            return;
        }
        if (insideSelection) {
            if (event->modifiers() & Qt::ControlModifier) {
                const QImage source = selectionSampleImage();
                if (m_marqueeTool.beginFill(point, source, m_doc.paintLayer())) {
                    m_doc.beginEdit();
                    m_doc.beginPaintTransaction();
                    updateSelection(point, event->modifiers());
                }
            } else {
                m_marqueeTool.beginMove(point);
            }
        } else {
            m_marqueeTool.beginCreate(m_doc.planes(), point);
        }
        update();
        return;
    }
    default:
        return; // 其余工具尚未实现
    }
}

void VpCanvas::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    if (m_panning) {
        m_offset += event->position() - m_panPoint;
        m_panPoint = event->position();
        updateViewTransform();
        return;
    }
    updateCursorPoint(event->position());
    if (m_marqueeTool.active() && (event->buttons() & Qt::LeftButton)) {
        updateSelection(m_cursorPoint, event->modifiers());
        return;
    }
    if (m_draggedFloatingImageIndex >= 0 && (event->buttons() & Qt::LeftButton)) {
        updateFloatingImageInteraction(m_cursorPoint, event->modifiers());
        return;
    }
    switch (m_controller.tool()) {
    case Tool::EditPlane: {
        if (m_editPlaneIndex < 0)
            return;
        PerspectivePlane candidate;
        if (!m_editTool.update(m_cursorPoint, &candidate))
            return;
        if (m_editTool.extruding()) {
            // 拉出垂直平面时源平面保持不动，候选几何只作为预览绘制
            m_extrudePreview = candidate;
            m_extrudePreviewReady = true;
        } else {
            m_doc.setPlane(m_editPlaneIndex, candidate);
            if (m_editTool.rotating())
                m_controller.notifyPlaneAngleChanged(); // 角度滑杆随拖动实时跟走
        }
        update();
        return;
    }
    case Tool::Brush:
        if (m_controller.brushDrawing()) {
            m_controller.moveBrush(m_cursorPoint);
            update();
        }
        return;
    case Tool::CloneStamp:
        if (m_controller.cloneDrawing()) {
            m_controller.moveClone(m_cursorPoint);
            update();
        }
        return;
    default:
        return;
    }
}

void VpCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if (m_panning) {
        m_offset += event->position() - m_panPoint;
        m_panning = false;
        updateViewTransform();
        updateNavigationCursor();
        return;
    }
    // 缩放/旋转的最终几何取松开时的位置，避免漏掉最后一次移动；图像拖动
    // 可以在任何工具下进行，所以先于工具分派收尾。
    if (m_draggedFloatingImageIndex >= 0)
        updateFloatingImageInteraction(widgetToImage(event->position()), event->modifiers());
    if (endFloatingImageInteraction())
        return;
    QMouseEvent finalMove(QEvent::MouseMove, event->position(), event->globalPosition(),
                          Qt::NoButton, Qt::LeftButton, event->modifiers());
    mouseMoveEvent(&finalMove);
    switch (m_controller.tool()) {
    case Tool::EditPlane: {
        if (m_editPlaneIndex < 0)
            return;
        const bool extruding = m_editTool.extruding();
        const int source = m_editPlaneIndex;
        const int edge = m_editTool.edgeIndex();
        m_editPlaneIndex = -1;
        // 普通拖动改的是源平面本身；拉出垂直平面则是新增一个平面
        bool changed = true;
        if (extruding)
            changed = extrudePlane(source, edge);
        m_extrudePreviewReady = false;
        m_doc.commitEdit(changed);
        update();
        return;
    }
    case Tool::Brush:
        if (m_controller.brushDrawing()) {
            m_controller.endBrush();
            update();
        }
        return;
    case Tool::CloneStamp:
        if (m_controller.cloneDrawing()) {
            m_controller.endClone();
            update();
        }
        return;
    case Tool::Marquee: {
        if (!m_marqueeTool.active())
            return;
        updateSelection(widgetToImage(event->position()), event->modifiers());
        if (m_marqueeTool.action() == MarqueeTool::Action::Fill) {
            // 克隆结果不留在绘画层：先丢弃拖动期间的预览像素，再落成一张浮动图像
            m_doc.cancelEdit();
            const int index = appendSelectionImage(m_marqueeTool.clone());
            if (index >= 0) {
                m_marqueeTool.clear();
                m_controller.postStatus(tr("已把拖动结果生成为浮动图像，可直接拖动移动。"));
                update();
                return;
            }
        }
        // 结束交互并清理取样快照；有效选区继续保留。
        m_marqueeTool.end();
        update();
        return;
    }
    default:
        return;
    }
}

// 记录光标位置；仿制源的取样指示与光标预览都依赖它，拖动期间同样要更新。
void VpCanvas::updateCursorPoint(const QPointF &widgetPoint)
{
    m_cursorPoint = widgetToImage(widgetPoint);
    if (m_controller.tool() == Tool::CloneStamp)
        m_controller.hoverClone(m_cursorPoint);
    if (cursorPreviewVisible())
        update();
}

// 橡皮筋、笔刷轮廓与仿制预览都随光标移动持续重绘。
void VpCanvas::hoverMoveEvent(QHoverEvent *event)
{
    updateCursorPoint(event->position());
    QQuickPaintedItem::hoverMoveEvent(event);
}

// 离开画布时清掉光标预览，避免预览停在最后位置。
void VpCanvas::hoverLeaveEvent(QHoverEvent *event)
{
    m_cursorPoint = QPointF();
    if (cursorPreviewVisible())
        update();
    QQuickPaintedItem::hoverLeaveEvent(event);
}

// 删除键按状态分派：创建平面时回退最后一个角点，否则删除选中的浮动图像，
// 没有图像再退到删除选中的平面。
void VpCanvas::undo()
{
    cancelInteraction();
    const bool changed = m_doc.undo();
    m_controller.notifyPlaneAngleChanged();
    m_controller.postStatus(changed ? tr("已撤销。") : tr("没有可撤销的操作。"));
    update();
}

void VpCanvas::redo()
{
    cancelInteraction();
    const bool changed = m_doc.redo();
    m_controller.notifyPlaneAngleChanged();
    m_controller.postStatus(changed ? tr("已重做。") : tr("没有可重做的操作。"));
    update();
}

void VpCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Alt) {
        updateNavigationCursor(true);
        event->accept();
        return;
    }
    const int key = event->key();
    if (key != Qt::Key_Backspace && key != Qt::Key_Delete) {
        QQuickPaintedItem::keyPressEvent(event);    // 其他键交给基类
        return;
    }
    if (m_createTool.creating()) {
        m_createTool.removeLastPoint();
        reportCreateProgress();
    } else if (m_doc.selectedFloatingImage() >= 0) {
        cancelInteraction();
        m_doc.removeFloatingImage(m_doc.selectedFloatingImage());
        m_controller.postStatus(tr("已删除选中的图像。"));
    } else {
        deleteSelectedPlane();
    }
    update();
    event->accept();
}

// 把控件坐标换算成图像坐标
QPointF VpCanvas::widgetToImage(const QPointF &widgetPoint) const
{
    return (widgetPoint - m_offset) / m_scale;
}

void VpCanvas::updateViewTransform()
{
    const QImage &background = m_doc.background();
    if (background.isNull() || width() <= 0 || height() <= 0)
        return;
    if (m_fitView) {
        const qreal sx = qMax(1.0, width() - ViewMargin * 2) / background.width();
        const qreal sy = qMax(1.0, height() - ViewMargin * 2) / background.height();
        m_scale = qBound(0.063, m_fillView ? qMax(sx, sy) : qMin(sx, sy), 16.0);
        m_offset = QPointF((width() - background.width() * m_scale) / 2,
                           (height() - background.height() * m_scale) / 2);
    }
    const QSizeF shown = QSizeF(background.size()) * m_scale;
    m_offset.setX(shown.width() <= width() ? (width() - shown.width()) / 2
                                         : qBound(width() - shown.width(), m_offset.x(), 0.0));
    m_offset.setY(shown.height() <= height() ? (height() - shown.height()) / 2
                                           : qBound(height() - shown.height(), m_offset.y(), 0.0));
    emit viewChanged();
    update();
}

qreal VpCanvas::horizontalSize() const
{
    return qMin(1.0, width() / qMax(1.0, m_doc.background().width() * m_scale));
}

qreal VpCanvas::verticalSize() const
{
    return qMin(1.0, height() / qMax(1.0, m_doc.background().height() * m_scale));
}

qreal VpCanvas::horizontalPosition() const
{
    return horizontalSize() >= 1 ? 0 : -m_offset.x() / (m_doc.background().width() * m_scale);
}

qreal VpCanvas::verticalPosition() const
{
    return verticalSize() >= 1 ? 0 : -m_offset.y() / (m_doc.background().height() * m_scale);
}

void VpCanvas::scrollTo(qreal horizontal, qreal vertical)
{
    m_fitView = false;
    m_offset = QPointF(-horizontal * m_doc.background().width() * m_scale,
                      -vertical * m_doc.background().height() * m_scale);
    updateViewTransform();
}

void VpCanvas::zoomAt(qreal scale, const QPointF &anchor)
{
    if (!std::isfinite(scale) || m_doc.background().isNull())
        return;
    const QPointF point = widgetToImage(anchor);
    m_fitView = false;
    m_scale = qBound(0.063, scale, 16.0);
    m_offset = anchor - point * m_scale;
    updateViewTransform();
}

void VpCanvas::setZoom(qreal scale)
{
    zoomAt(scale, QPointF(width() / 2, height() / 2));
}

void VpCanvas::stepAt(bool out, const QPointF &anchor)
{
    static constexpr qreal levels[] = {0.063, 0.125, 0.25, 0.333, 0.5, 0.667,
                                      1, 2, 3, 4, 6, 8, 10, 12, 16};
    int nearest = 0;
    for (int i = 1; i < 15; ++i)
        if (std::abs(levels[i] - m_scale) < std::abs(levels[nearest] - m_scale))
            nearest = i;
    zoomAt(levels[qBound(0, nearest + (out ? -1 : 1), 14)], anchor);
}

void VpCanvas::zoomStep(bool out)
{
    stepAt(out, QPointF(width() / 2, height() / 2));
}

void VpCanvas::fitView(bool fill)
{
    m_fitView = true;
    m_fillView = fill;
    updateViewTransform();
}

void VpCanvas::updateNavigationCursor(bool alt)
{
    if (m_controller.tool() == Tool::Hand) {
        setCursor(m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    } else if (m_controller.tool() == Tool::Zoom) {
        QPixmap icon(32, 32);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 3));
        painter.drawEllipse(QRectF(3, 3, 18, 18));
        painter.drawLine(19, 19, 28, 28);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawEllipse(QRectF(3, 3, 18, 18));
        painter.drawLine(19, 19, 28, 28);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawLine(7, 12, 17, 12);
        if (!alt)
            painter.drawLine(12, 7, 12, 17);
        painter.end();
        setCursor(QCursor(icon, 12, 12));
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void VpCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Alt) {
        updateNavigationCursor(false);
        event->accept();
        return;
    }
    QQuickPaintedItem::keyReleaseEvent(event);
}

void VpCanvas::finishPlaneCreation()
{
    const PerspectivePlane plane = m_createTool.makePlane(m_doc.nextSurfaceGroupId());
    m_createTool.reset();

    if (!plane.quad().isValid()) {
        m_controller.postStatus(tr("无法创建：四个点必须依次组成非交叉的凸四边形，请重新设置。"));
        return;
    }
    m_doc.beginEdit();
    const int index = m_doc.appendPlane(plane);
    m_doc.setSelectedPlane(index);
    m_doc.commitEdit(true);

    m_controller.setTool(Tool::EditPlane);
    m_controller.notifyPlaneAngleChanged(); // 新平面是独立平面，夹角滑杆转为不可调
    m_controller.postStatus(tr("平面已创建，已自动进入编辑平面工具。"));
}

// 把拖动预览落成一个与源平面垂直的新平面；返回是否真的产生了新平面。
bool VpCanvas::extrudePlane(int sourcePlane, int edge)
{
    if (!m_extrudePreviewReady)
        return false;
    PerspectivePlane plane = m_extrudePreview;
    // 拖出的面积太小当成误操作，不落盘
    const QRectF bounds = plane.quad().canvasPolygon().boundingRect();
    if (qAbs(bounds.width() * bounds.height()) <= 100.0)
        return false;
    plane.setParent(sourcePlane, edge); // 父子关系：删除平面时靠它解锁共用边
    plane.setAngleToParentDegrees(90.0);
    plane.setHasCustomAngle(false);
    if (m_doc.appendPlane(plane) < 0)
        return false;
    m_doc.setSelectedPlane(m_doc.planes().size() - 1);
    m_doc.lockPlaneEdge(sourcePlane, edge); // 共用边在源平面上不能再编辑
    m_controller.notifyPlaneAngleChanged(); // 选中项变成新的子平面，夹角滑杆转为可用
    m_controller.postStatus(tr("已拉出垂直平面。"));
    return true;
}

void VpCanvas::cancelInteraction()
{
    m_panning = false;
    m_createTool.reset();
    m_extrudePreviewReady = false; // 拖出垂直平面的预览随交互一起作废
    // 进行中的笔触已经烘焙进绘画层，切换工具时提交。
    m_controller.finishActiveStrokes();
    // 平面拖动、图像拖动与 Ctrl 克隆都只改了结构或绘画预览，可以直接丢弃
    if (m_editPlaneIndex >= 0 || m_draggedFloatingImageIndex >= 0
        || m_marqueeTool.active()) {
        m_editPlaneIndex = -1;
        m_floatingImageTransform.reset();
        m_draggedFloatingImageIndex = -1;
        m_floatingImageChanged = false;
        m_doc.cancelEdit();
    }
    m_marqueeTool.clear();
}

// 是否需要画光标预览（创建平面的橡皮筋、画笔与图章的光标预览）
bool VpCanvas::cursorPreviewVisible() const
{
    if (m_controller.tool() == Tool::Brush)
        return true;
    if (m_controller.tool() == Tool::CloneStamp)
        return m_controller.hasCloneSource();
    return m_controller.tool() == Tool::CreatePlane && m_createTool.creating();
}

// 删除当前选中的平面；没有选中时什么也不做。
void VpCanvas::deleteSelectedPlane()
{
    // 拖动编辑进行中：先丢弃这次拖动，否则后续鼠标事件会写回已经移位的平面下标
    if (m_editPlaneIndex >= 0)
        cancelInteraction();
    const int index = m_doc.selectedPlane();
    if (index < 0)
        return;
    m_doc.removePlane(index);
    m_controller.notifyPlaneAngleChanged(); // 删除会改变选中项，也可能解开上一级父平面的夹角锁定
    m_controller.postStatus(tr("已删除选中的平面。"));
    update();
}

// 状态栏提示创建进度；角点被回退干净时给出独立提示。
void VpCanvas::reportCreateProgress()
{
    const int count = m_createTool.points().size();
    if (count == 0)
        m_controller.postStatus(tr("已回退全部角点，请重新点击"));
    else
        m_controller.postStatus(tr("已设置 %1/%2 个角点").arg(count).arg(PlaneCreateTool::CornerCount));
}

// 按下时的图像处理。返回 true 表示这次按下已被图像消费，工具不再响应。
// 顺序与 Photoshop 一致：先试控制点（仅变换工具），再试图像本体，最后才轮到烘焙。
bool VpCanvas::beginFloatingImageInteraction(const QPointF &point)
{
    if (m_controller.tool() == Tool::Transform && m_doc.selectedFloatingImage() >= 0) {
        const FloatingImage &image = m_doc.floatingImage(m_doc.selectedFloatingImage());
        const int handle = FloatingImageTransformTool::handleAt(image, point, m_scale);
        const int corner = handle < 0
            ? FloatingImageTransformTool::rotationCornerAt(image, point, m_scale) : -1;
        const auto mode = corner >= 0 ? FloatingImageTransformTool::Mode::Rotate
                                      : FloatingImageTransformTool::Mode::Scale;
        if ((handle >= 0 || corner >= 0)
            && m_floatingImageTransform.beginTransform(
                image, point, corner >= 0 ? corner : handle, mode)) {
            m_draggedFloatingImageIndex = m_doc.selectedFloatingImage();
            m_doc.beginEdit();
            update();
            return true;
        }
    }

    QPointF grabOffset;
    int grabbed = -1;
    const bool hitImage = m_doc.background().rect().contains(point.toPoint())
                          && floatingImageAt(point, &grabbed, &grabOffset);
    if (!hitImage && m_doc.selectedFloatingImage() >= 0) {
        // 点到别处：把选中的图像烘焙进绘画层，之后不再是可操作对象。
        // 这一下点击只用于确认烘焙，不再触发工具的其它动作，避免误落一笔。
        bakeSelectedFloatingImage();
        return true;
    }
    if (hitImage) {
        // 选框工具下只有已选中的图像才拦截点击；点到未选中的图像留给选区建立，
        // 否则贴过图的平面上就再也拖不出新选区。
        if (m_controller.tool() == Tool::Marquee && grabbed != m_doc.selectedFloatingImage())
            return false;
        m_draggedFloatingImageIndex = grabbed;
        m_doc.setSelectedFloatingImage(grabbed);
        m_doc.beginEdit();
        m_floatingImageTransform.beginMove(m_doc.floatingImage(grabbed), grabOffset);
        update();
        return true;
    }
    return false;
}

// 拖动中的图像：缩放/旋转交给工具自己算新几何；平移则沿快照曲面滑动（变换工具下
// 已吸附的图像），或者按光标所在的平面吸附，落在平面外时脱离回画布坐标。
// 越过极点线映射不出来时保持原位。
void VpCanvas::updateFloatingImageInteraction(const QPointF &point,
                                              Qt::KeyboardModifiers modifiers)
{
    if (m_draggedFloatingImageIndex < 0 || m_draggedFloatingImageIndex >= m_doc.floatingImages().size()) {
        cancelInteraction();
        return;
    }
    if (m_floatingImageTransform.isTransforming()) {
        FloatingImage image;
        if (m_floatingImageTransform.update(
                point, modifiers & Qt::ShiftModifier, modifiers & Qt::AltModifier, &image)) {
            m_doc.setFloatingImage(m_draggedFloatingImageIndex, image);
            // 与起始几何比对后才算改动：按住控制点原地松手不该占一格历史
            const FloatingImage &start = m_floatingImageTransform.startImage();
            m_floatingImageChanged = image.placementOrigin != start.placementOrigin
                                     || image.scaleFactors != start.scaleFactors
                                     || image.rotationDegrees != start.rotationDegrees;
        }
        update();
        return;
    }
    const FloatingImage &start = m_floatingImageTransform.startImage();
    if (m_controller.tool() == Tool::Transform && start.surfaceAttached) {
        QPointF surface;
        if (start.mapCanvasToPlacement(point, &surface)) {
            m_doc.setFloatingImageOrigin(
                m_draggedFloatingImageIndex,
                surface - m_floatingImageTransform.grabOffset());
            // 沿曲面滑动可能原地不动（越过极点线时保持原位），不算一次改动
            m_floatingImageChanged =
                m_doc.floatingImage(m_draggedFloatingImageIndex).placementOrigin
                != start.placementOrigin;
        }
    } else if (const int plane = topmostPlaneIndexAt(m_doc.planes(), point); plane >= 0) {
        attachFloatingImageToPlane(m_draggedFloatingImageIndex, plane, point);
        m_floatingImageChanged = true;
    } else if (!m_doc.floatingImage(m_draggedFloatingImageIndex).surfaceAttached
               || !moveSurfaceAttachedImage(m_draggedFloatingImageIndex, point)) {
        m_doc.detachFloatingImage(
            m_draggedFloatingImageIndex, point - m_floatingImageTransform.grabOffset());
        m_floatingImageChanged = true;
    }
    update();
}

// 结束拖动并提交；返回 false 表示当时没有图像在拖动。
bool VpCanvas::endFloatingImageInteraction()
{
    if (m_draggedFloatingImageIndex < 0)
        return false;
    m_floatingImageTransform.reset();
    m_draggedFloatingImageIndex = -1;
    m_doc.commitEdit(true); // 只是点了一下、几何没变就不占一格历史
    m_floatingImageChanged = false;
    update();
    return true;
}

// 命中测试：判断画布坐标是否落在某张浮动图像上（从最上层开始），
// 命中时返回该点相对图像左上角的抓取偏移。
bool VpCanvas::floatingImageAt(const QPointF &point, int *index, QPointF *grabOffset) const
{
    for (int i = m_doc.floatingImages().size() - 1; i >= 0; --i) {
        if (FloatingImageProjection::forImage(m_doc.floatingImage(i))->hitTest(point, grabOffset)) {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

// 把图像吸附到目标平面所在的曲面分组：拷贝该分组的全部面片几何作为严格快照，
// 此后平面的增删改都不再影响它。
void VpCanvas::attachFloatingImageToPlane(int index, int planeIndex, const QPointF &point)
{
    const PerspectivePlane &host = m_doc.planes()[planeIndex];
    QVector<PerspectiveQuad> surfaceQuads;
    int hostQuadIndex = -1;
    for (int i = 0; i < m_doc.planes().size(); ++i) {
        const PerspectivePlane &plane = m_doc.planes()[i];
        if (plane.surfaceGroupId() != host.surfaceGroupId())
            continue;
        const PerspectiveQuad &quad = plane.quad();
        if (i == planeIndex)
            hostQuadIndex = surfaceQuads.size();
        surfaceQuads.append(quad);
    }
    QPointF surfacePoint;
    if (!host.quad().surfaceToCanvasTransform().mapInverse(point, &surfacePoint))
        return;
    m_doc.attachFloatingImage(
        index, surfaceQuads, hostQuadIndex,
        surfacePoint - m_floatingImageTransform.grabOffset());
}

// 已吸附的图像沿它的快照曲面移动：优先用包住光标的那个面片，都不包住时退回宿主面片
// 外推；连外推都映射不出来（越过极点线）才返回 false。
bool VpCanvas::moveSurfaceAttachedImage(int index, const QPointF &point)
{
    const FloatingImage &image = m_doc.floatingImage(index);
    auto moveOnQuad = [this, index, &point](const PerspectiveQuad &quad) {
        QPointF surfacePoint;
        const bool ok = quad.surfaceToCanvasTransform().mapInverse(point, &surfacePoint);
        if (ok)
            m_doc.setFloatingImageOrigin(
                index, surfacePoint - m_floatingImageTransform.grabOffset());
        return ok;
    };
    for (auto quad = image.surfaceQuads.crbegin(); quad != image.surfaceQuads.crend(); ++quad) {
        if (quad->containsCanvasPoint(point)
            && moveOnQuad(*quad))
            return true;
    }
    return image.hostQuadIndex >= 0 && image.hostQuadIndex < image.surfaceQuads.size()
           && moveOnQuad(image.surfaceQuads[image.hostQuadIndex]);
}

// 把当前选中的浮动图像按当前几何画进绘画层并删除它：走的是与屏幕渲染完全相同的
// 分段投影路径，因此肉眼看不到像素跳变。烘焙后内容并入绘画层，不再能单独操作。
void VpCanvas::bakeSelectedFloatingImage()
{
    if (m_doc.selectedFloatingImage() < 0)
        return;
    const int index = m_doc.selectedFloatingImage();
    m_doc.beginEdit();
    m_doc.beginPaintTransaction();
    QPainter painter(&m_doc.paintLayer());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QRect dirty;
    for (int i = 0; i <= index; ++i) {
        const FloatingImage &image = m_doc.floatingImage(i);
        SceneRenderer(m_doc).renderFloatingImage(painter, image);
        dirty = dirty.united(FloatingImageProjection::forImage(image)->canvasOutline()
                            .boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2));
    }
    painter.end();
    m_doc.addPaintDirty(dirty.intersected(m_doc.paintLayer().rect()));
    for (int i = index; i >= 0; --i)
        m_doc.removeFloatingImage(i);
    m_doc.commitEdit(true);
    m_floatingImageTransform.reset();
    m_draggedFloatingImageIndex = -1;
    m_floatingImageChanged = false;
    m_controller.postStatus(tr("浮动图像已合并到绘画层。"));
    update();
}

// 变换工具下画出浮动图像的 8 个控制点；尺寸与线宽按视图缩放换算，屏幕上恒定。
void VpCanvas::drawFloatingImageHandles(QPainter *painter)
{
    if (m_controller.tool() != Tool::Transform || m_doc.selectedFloatingImage() < 0)
        return;
    const auto projection = FloatingImageProjection::forImage(
        m_doc.floatingImage(m_doc.selectedFloatingImage()));
    const qreal half = 4.0 / m_scale;
    painter->save();
    painter->setPen(QPen(QColor("#1769aa"), 1.0 / m_scale));
    painter->setBrush(Qt::white);
    for (const QPointF &control : projection->transformHandles())
        painter->drawRect(QRectF(control.x() - half, control.y() - half, half * 2, half * 2));
    painter->restore();
}

// 取样和文档事务由画布协调，选区工具只负责计算。
QImage VpCanvas::selectionSampleImage() const
{
    QImage source(m_doc.background().size(), QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::transparent);
    QPainter painter(&source);
    SceneRenderer(m_doc).render(painter, 1.0, /*showGuides*/ false);
    painter.end();
    return source;
}

int VpCanvas::appendSelectionImage(const FloatingImage &image)
{
    if (image.bitmap.isNull())
        return -1;
    return m_doc.addFloatingImageOnSurface(image.bitmap, image.surfaceQuads,
                                          image.hostQuadIndex, image.placementOrigin);
}

void VpCanvas::updateSelection(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    const QRect dirty = m_marqueeTool.update(point, modifiers, m_controller.gridSize(),
                                           &m_doc.paintLayer());
    if (!dirty.isEmpty())
        m_doc.addPaintDirty(dirty);
    update();
}

// 选区轮廓按控件坐标描边（先 resetTransform 再整体缩放），
// 白 1px 打底 + 黑虚线，线宽与虚线间距在屏幕上恒定。
void VpCanvas::drawSelectionOutline(QPainter *painter)
{
    if (m_controller.tool() != Tool::Marquee || m_marqueeTool.rect().isEmpty())
        return;
    painter->save();
    painter->resetTransform();
    const QPainterPath outline =
        QTransform::fromTranslate(m_offset.x(), m_offset.y())
            .map(QTransform::fromScale(m_scale, m_scale).map(m_marqueeTool.outline()));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(Qt::white, 1));
    painter->drawPath(outline);
    QPen pen(Qt::black, 1);
    pen.setDashPattern({4, 4});
    pen.setDashOffset(m_antsPhase);
    painter->setPen(pen);
    painter->drawPath(outline);
    painter->restore();
}
