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
                                m_createTool.points(), nullptr,
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
    event->accept();
    const QPointF point = widgetToImage(event->position());
    if (m_tool == Tool::EditPlane) {
        const qreal tolerance = qMax(8.0 / qMax(m_scale, 1e-6), 4.0);
        int planeIndex = -1;
        int handle = -1;
        // 先查控制点，允许鼠标落在平面边界外的容差范围内。
        for (int i = m_doc.planes().size() - 1; i >= 0; --i) {
            const int candidateHandle = PlaneMath::handleAt(m_doc.planes()[i], point, tolerance);
            if (candidateHandle >= 0) {
                planeIndex = i;
                handle = candidateHandle;
                break;
            }
        }
        if (planeIndex < 0)
            planeIndex = PlaneMath::planeAt(m_doc.planes(), point);
        if (planeIndex < 0) {
            m_doc.setSelectedPlane(-1);
            update();
            return;
        }

        m_doc.setSelectedPlane(planeIndex);
        const Plane &plane = m_doc.planes()[planeIndex];
        if (handle < 0)
            handle = PlaneMath::handleAt(plane, point, tolerance);
        const int edge = handle >= 4 ? handle - 4 : -1;
        m_editPlaneIndex = planeIndex;
        m_editTool.begin(plane, point, handle, edge, false, m_doc.background().size());
        m_doc.beginEdit();
        update();
        return;
    }
    if (m_tool != Tool::CreatePlane)
        return;

    m_createTool.addPoint(point);
    if (m_createTool.complete())
        finishPlaneCreation();
    else
        reportCreateProgress();
    update();
}

void VpCanvas::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    if (m_tool != Tool::EditPlane || m_editPlaneIndex < 0)
        return;
    Plane candidate;
    if (m_editTool.update(widgetToImage(event->position()), &candidate)) {
        m_doc.setPlane(m_editPlaneIndex, candidate);
        update();
    }
}

void VpCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if (m_tool != Tool::EditPlane || m_editPlaneIndex < 0)
        return;
    m_editPlaneIndex = -1;
    m_doc.commitEdit(true);
    update();
}

// 橡皮筋预览随光标移动持续重绘。
void VpCanvas::hoverMoveEvent(QHoverEvent *event)
{
    m_cursorPoint = widgetToImage(event->position());
    if (m_tool == Tool::CreatePlane && m_createTool.active())
        update();
    QQuickPaintedItem::hoverMoveEvent(event);
}

// 离开画布时清掉光标预览，避免橡皮筋停在最后位置。
void VpCanvas::hoverLeaveEvent(QHoverEvent *event)
{
    m_cursorPoint = QPointF();
    if (m_tool == Tool::CreatePlane && m_createTool.active())
        update();
    QQuickPaintedItem::hoverLeaveEvent(event);
}

void VpCanvas::keyPressEvent(QKeyEvent *event)
{
    if (m_createTool.active()) {    //  创建平面过程中：Backspace / Delete 回退最后一个角点。
        switch (event->key()) {
        case Qt::Key_Backspace:
        case Qt::Key_Delete:
            m_createTool.removeLastPoint();
            reportCreateProgress();
            break;
        default:
            QQuickPaintedItem::keyPressEvent(event);
            return;
        }
        update();
        event->accept();
        return;
    }
    QQuickPaintedItem::keyPressEvent(event);
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
    const Plane plane = m_createTool.makePlane(
        m_doc.nextSurfaceGroupId(), tr("平面 %1").arg(m_doc.planes().size() + 1));
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

void VpCanvas::cancelInteraction()
{
    m_createTool.reset();
    if (m_editPlaneIndex >= 0) {
        m_editPlaneIndex = -1;
        m_doc.cancelEdit();
    }
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
