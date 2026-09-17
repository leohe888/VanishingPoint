#include "vpcanvas.h"

#include "core/scenerenderer.h"

#include <QCursor>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace {
constexpr qreal ViewMargin = 16.0; // 图像与画布边缘的留白
}

VpCanvas::VpCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptHoverEvents(true); // 接受鼠标悬停事件
    setAcceptedMouseButtons(Qt::LeftButton); // 接受鼠标左键
    setActiveFocusOnTab(true);  // 允许通过 Tab 键获得焦点
    setAntialiasing(true);  // 开启抗锯齿
    setCursor(Qt::ArrowCursor); // 光标统一用箭头

    constexpr auto DefaultBackgroundPath = R"(C:\Users\yixin\Pictures\3.jpg)";
    m_doc.loadImage(QString::fromUtf8(DefaultBackgroundPath));  // 启动时加载默认背景

    updateViewTransform();
}

VpCanvas::Tool VpCanvas::tool() const
{
    return m_tool;
}

// 切换工具：放弃进行中的交互，更新光标与提示。
void VpCanvas::setTool(Tool tool)
{
    const auto statusForTool = [tool]() {
        switch (tool) {
        case Tool::CreatePlane: return QObject::tr("依次单击四个角点以创建平面");
        case Tool::EditPlane: return QObject::tr("拖动控制点或边缘以编辑平面");
        case Tool::Marquee: return QObject::tr("拖动以框选画布区域");
        case Tool::CloneStamp: return QObject::tr("选择源区域并在目标位置绘制");
        case Tool::Brush: return QObject::tr("拖动以绘制笔触");
        case Tool::Transform: return QObject::tr("拖动控制点以变换图像");
        }
        return QString();
    };

    if (tool == m_tool) {
        emit statusMessage(statusForTool());
        return;
    }
    cancelInteraction();
    m_tool = tool;
    emit statusMessage(statusForTool());
    emit toolChanged();
    update();
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
                                /*editHandlesVisible*/ m_tool == Tool::EditPlane,
                                /*hoveredPlane*/ -1, /*antsPhase*/ 0, /*drawContent*/ true,
                                /*gridSize*/ 50.0,
                                m_tool == Tool::CreatePlane ? m_cursorPoint : QPointF());
    painter->restore();
}

void VpCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);    // 必须调用基类实现
    if (newGeometry.size() != oldGeometry.size())
        updateViewTransform();
}

void VpCanvas::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(); // 获得键盘焦点
    event->accept();    // 标记事件已处理
    const QPointF point = widgetToImage(event->position());
    switch (m_tool) {
    case Tool::EditPlane: {
        const qreal tolerance = qMax(8.0 / qMax(m_scale, 1e-6), 4.0);
        int planeIndex = -1;
        int handle = -1;
        // 先检测控制点。后创建的平面在上层，先被检查
        for (int i = m_doc.planes().size() - 1; i >= 0; --i) {
            const int candidateHandle = PlaneMath::handleAt(m_doc.planes()[i], point, tolerance);
            if (candidateHandle >= 0) {
                planeIndex = i;
                handle = candidateHandle;
                break;
            }
        }
        // 如果没点到控制点，再检测平面本体
        if (planeIndex < 0)
            planeIndex = PlaneMath::planeAt(m_doc.planes(), point);

        // 什么都没点到，则取消选择
        if (planeIndex < 0) {
            m_doc.setSelectedPlane(-1);
            update();
            return;
        }

        // 选中平面
        m_doc.setSelectedPlane(planeIndex);
        // 与相邻平面共边的平面不能整体平移，否则共用边会被撕开
        if (handle < 0 && m_doc.isPlaneLinked(planeIndex)) {
            emit statusMessage(tr("该平面已与相邻平面共边，不能整体移动。"));
            update();
            return;
        }
        const Plane &plane = m_doc.planes()[planeIndex];
        const int edge = handle >= 4 ? handle - 4 : -1;
        // Ctrl + 拖动边中点：从这条边拖出一个与之垂直的新平面
        const bool extrude = edge >= 0 && (event->modifiers() & Qt::ControlModifier);
        m_editPlaneIndex = planeIndex;
        m_extrudePreviewReady = false;
        m_editTool.begin(plane, point, handle, edge, extrude, m_doc.background().size());
        m_doc.beginEdit();
        if (extrude)
            emit statusMessage(tr("拖动以拉出垂直平面，松开完成。"));
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
    default:
        return; // 其余工具尚未实现
    }
}

void VpCanvas::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    if (m_tool != Tool::EditPlane || m_editPlaneIndex < 0)
        return;
    Plane candidate;
    if (!m_editTool.update(widgetToImage(event->position()), &candidate))
        return;
    if (m_editTool.extruding()) {
        // 拉出垂直平面时源平面保持不动，候选几何只作为预览绘制
        m_extrudePreview = candidate;
        m_extrudePreviewReady = true;
    } else {
        m_doc.setPlane(m_editPlaneIndex, candidate);
    }
    update();
}

void VpCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if (m_tool != Tool::EditPlane || m_editPlaneIndex < 0)
        return;
    const bool extruding = m_editTool.extruding();
    const int source = m_editPlaneIndex;
    const int edge = m_editTool.edge();
    m_editPlaneIndex = -1;
    // 普通拖动改的是源平面本身；拉出垂直平面则是新增一个平面
    bool changed = true;
    if (extruding)
        changed = extrudePlane(source, edge);
    m_extrudePreviewReady = false;
    m_doc.commitEdit(changed);
    update();
}

// 橡皮筋预览随光标移动持续重绘。
void VpCanvas::hoverMoveEvent(QHoverEvent *event)
{
    m_cursorPoint = widgetToImage(event->position());
    if (m_tool == Tool::CreatePlane && m_createTool.creating())
        update();
    QQuickPaintedItem::hoverMoveEvent(event);
}

// 离开画布时清掉光标预览，避免橡皮筋停在最后位置。
void VpCanvas::hoverLeaveEvent(QHoverEvent *event)
{
    m_cursorPoint = QPointF();
    if (m_tool == Tool::CreatePlane && m_createTool.creating())
        update();
    QQuickPaintedItem::hoverLeaveEvent(event);
}

// 删除键：创建平面时回退最后一个角点，其余情况删除当前选中的平面。
void VpCanvas::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key != Qt::Key_Backspace && key != Qt::Key_Delete) {
        QQuickPaintedItem::keyPressEvent(event);    // 其他键交给基类
        return;
    }
    if (m_createTool.creating()) {
        m_createTool.removeLastPoint();
        reportCreateProgress();
        update();
    } else {
        deleteSelectedPlane();
    }
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
    const qreal sx = (width() - ViewMargin * 2) / background.width();   // 水平缩放比
    const qreal sy = (height() - ViewMargin * 2) / background.height(); // 垂直缩放比
    m_scale = qMin(sx, sy); // 缩放比取水平缩放比和垂直缩放比的最小值
    if (m_scale <= 0)
        m_scale = 1.0;
    const QSizeF shown = QSizeF(background.size()) * m_scale;   // 计算缩放后图片尺寸
    m_offset = QPointF((width() - shown.width()) / 2.0, (height() - shown.height()) / 2.0); // 计算偏移量，使图片在画布中居中
}

void VpCanvas::finishPlaneCreation()
{
    const Plane plane = m_createTool.makePlane(m_doc.nextSurfaceGroupId());
    m_createTool.reset();

    if (!PlaneMath::isValidPlane(plane)) {
        emit statusMessage(tr("无法创建：四个点必须依次组成非交叉的凸四边形，请重新设置。"));
        return;
    }
    m_doc.beginEdit();
    const int index = m_doc.appendPlane(plane);
    m_doc.setSelectedPlane(index);
    m_doc.commitEdit(true);

    setTool(Tool::EditPlane);
    emit statusMessage(tr("平面已创建，已自动进入编辑平面工具。"));
}

// 把拖动预览落成一个与源平面垂直的新平面；返回是否真的产生了新平面。
bool VpCanvas::extrudePlane(int sourcePlane, int edge)
{
    if (!m_extrudePreviewReady)
        return false;
    Plane plane = m_extrudePreview;
    plane.parentPlane = sourcePlane; // 父子关系：删除平面时靠它解锁共用边
    plane.parentEdge = edge;
    if (m_doc.appendPlane(plane) < 0)
        return false;
    m_doc.setSelectedPlane(m_doc.planes().size() - 1);
    m_doc.lockPlaneEdge(sourcePlane, edge); // 共用边在源平面上不能再编辑
    emit statusMessage(tr("已拉出垂直平面。"));
    return true;
}

void VpCanvas::cancelInteraction()
{
    m_createTool.reset();
    m_extrudePreviewReady = false; // 拖出垂直平面的预览随交互一起作废
    if (m_editPlaneIndex >= 0) {
        m_editPlaneIndex = -1;
        m_doc.cancelEdit();
    }
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
    emit statusMessage(tr("已删除选中的平面。"));
    update();
}

// 状态栏提示创建进度；角点被回退干净时给出独立提示。
void VpCanvas::reportCreateProgress()
{
    const int count = m_createTool.points().size();
    if (count == 0)
        emit statusMessage(tr("已回退全部角点，请重新点击"));
    else
        emit statusMessage(tr("已设置 %1/%2 个角点").arg(count).arg(PlaneCreateTool::CornerCount));
}
