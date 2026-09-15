#include "vpcanvas.h"

#include "core/scenerenderer.h"

#include <QCursor>
#include <QHash>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace {
// 工具枚举与 QML 短名的对应表，新增工具时在此登记即可。
const QHash<QString, VpCanvas::Tool> &toolNames()
{
    static const QHash<QString, VpCanvas::Tool> table {
        { QStringLiteral("create"), VpCanvas::Tool::CreatePlane },
        { QStringLiteral("edit"), VpCanvas::Tool::EditPlane },
        { QStringLiteral("marquee"), VpCanvas::Tool::Marquee },
        { QStringLiteral("stamp"), VpCanvas::Tool::CloneStamp },
        { QStringLiteral("brush"), VpCanvas::Tool::Brush },
        { QStringLiteral("transform"), VpCanvas::Tool::Transform },
    };
    return table;
}

constexpr qreal ViewMargin = 16.0; // 图像与控件边缘的留白（控件像素）
}

VpCanvas::VpCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
    setAntialiasing(true);
    setCursor(Qt::CrossCursor);

    // 未打开图片前先给一块默认画布，使视图变换与平面创建可以直接工作。
    QImage background(1200, 800, QImage::Format_ARGB32);
    background.fill(QColor("#26292d"));
    m_doc.setBackground(background);
    updateViewTransform();
}

QString VpCanvas::tool() const
{
    return toolNames().key(m_tool);
}

void VpCanvas::setTool(const QString &name)
{
    const auto it = toolNames().constFind(name);
    if (it != toolNames().constEnd())
        setTool(it.value());
}

// 切换工具：放弃进行中的交互，更新光标与提示。
void VpCanvas::setTool(Tool tool)
{
    if (tool == m_tool)
        return;
    cancelInteraction();
    m_tool = tool;
    setCursor(tool == Tool::EditPlane ? Qt::SizeAllCursor : Qt::CrossCursor);
    if (tool == Tool::CreatePlane)
        emit statusMessage(tr("依次单击四个角点以创建平面"));
    emit toolChanged();
    update();
}

void VpCanvas::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), QColor("#191b1e"));
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
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        updateViewTransform();
}

void VpCanvas::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus();
    event->accept();
    if (m_tool != Tool::CreatePlane)
        return;

    m_createTool.addPoint(toImage(event->position()));
    if (m_createTool.complete()) {
        finishPlaneCreation();
    } else {
        emit statusMessage(tr("已设置 %1/%2 个角点")
                               .arg(m_createTool.points().size())
                               .arg(PlaneCreateTool::CornerCount));
    }
    update();
}

// 橡皮筋预览随光标移动持续重绘。
void VpCanvas::hoverMoveEvent(QHoverEvent *event)
{
    m_cursorPoint = toImage(event->position());
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
    if (event->key() == Qt::Key_Escape && m_createTool.active()) {
        cancelInteraction();
        emit statusMessage(tr("已取消创建平面"));
        update();
        event->accept();
        return;
    }
    QQuickPaintedItem::keyPressEvent(event);
}

QPointF VpCanvas::toImage(const QPointF &widgetPoint) const
{
    return (widgetPoint - m_offset) / m_scale;
}

void VpCanvas::updateViewTransform()
{
    const QImage &background = m_doc.background();
    if (background.isNull() || width() <= 0 || height() <= 0)
        return;
    const qreal sx = (width() - ViewMargin * 2) / background.width();
    const qreal sy = (height() - ViewMargin * 2) / background.height();
    m_scale = qMin(sx, sy);
    if (m_scale <= 0)
        m_scale = 1.0;
    const QSizeF shown = QSizeF(background.size()) * m_scale;
    m_offset = QPointF((width() - shown.width()) / 2.0, (height() - shown.height()) / 2.0);
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
}
