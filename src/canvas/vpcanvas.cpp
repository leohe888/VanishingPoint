#include "vpcanvas.h"

#include "core/imagegeometry.h"
#include "core/scenerenderer.h"

#include <QCursor>
#include <QDataStream>
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
        case Tool::CloneStamp: return QObject::tr("按住 Alt 单击设置仿制源，再在目标位置绘制");
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

int VpCanvas::brushDiameter() const
{
    return m_brushTool.diameter();
}

// 越界值由引擎钳到合法区间
void VpCanvas::setBrushDiameter(int value)
{
    if (m_brushTool.diameter() == value)
        return;
    m_brushTool.setDiameter(value);
    emit brushChanged();
}

int VpCanvas::brushHardness() const
{
    return m_brushTool.hardness();
}

void VpCanvas::setBrushHardness(int value)
{
    if (m_brushTool.hardness() == value)
        return;
    m_brushTool.setHardness(value);
    emit brushChanged();
}

int VpCanvas::brushOpacity() const
{
    return m_brushTool.opacity();
}

void VpCanvas::setBrushOpacity(int value)
{
    if (m_brushTool.opacity() == value)
        return;
    m_brushTool.setOpacity(value);
    emit brushChanged();
}

QColor VpCanvas::brushColor() const
{
    return m_brushTool.color();
}

void VpCanvas::setBrushColor(const QColor &color)
{
    if (m_brushTool.color() == color)
        return;
    m_brushTool.setColor(color);
    emit brushChanged();
}

int VpCanvas::cloneDiameter() const
{
    return m_cloneTool.diameter();
}

// 越界值由引擎钳到合法区间
void VpCanvas::setCloneDiameter(int value)
{
    if (m_cloneTool.diameter() == value)
        return;
    m_cloneTool.setDiameter(value);
    emit cloneChanged();
}

int VpCanvas::cloneHardness() const
{
    return m_cloneTool.hardness();
}

void VpCanvas::setCloneHardness(int value)
{
    if (m_cloneTool.hardness() == value)
        return;
    m_cloneTool.setHardness(value);
    emit cloneChanged();
}

int VpCanvas::cloneOpacity() const
{
    return m_cloneTool.opacity();
}

void VpCanvas::setCloneOpacity(int value)
{
    if (m_cloneTool.opacity() == value)
        return;
    m_cloneTool.setOpacity(value);
    emit cloneChanged();
}

bool VpCanvas::cloneAligned() const
{
    return m_cloneTool.aligned();
}

void VpCanvas::setCloneAligned(bool aligned)
{
    if (m_cloneTool.aligned() == aligned)
        return;
    m_cloneTool.setAligned(aligned);
    emit cloneChanged();
    update();
}

// 仿制取样的内容 = 背景 + 绘画层 + 浮动图像，即画面上看到的全部内容。
// 按内容键缓存；落笔期间冻结，使整笔都取自同一份快照，也免得每次移动都重铺一遍。
const QImage &VpCanvas::cloneSource()
{
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << m_doc.background().cacheKey() << m_doc.paintLayer().cacheKey()
           << qint64(m_doc.images().size());
    for (const FloatingImage &image : m_doc.images())
        stream << image.image.cacheKey() << ImageGeometry::key(image);
    if (m_cloneTool.drawing() || (key == m_cloneSourceKey && !m_cloneSource.isNull()))
        return m_cloneSource;
    m_cloneSourceKey = key;
    m_cloneSource = QImage(m_doc.background().size(), QImage::Format_ARGB32_Premultiplied);
    m_cloneSource.fill(Qt::transparent);
    QPainter painter(&m_cloneSource);
    SceneRenderer(m_doc).render(painter, 1.0, /*showGuides*/ false);
    return m_cloneSource;
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
    // 光标离开画布时不画预览：空点 (0,0) 同时也是合法的图像坐标
    const bool cursorOnCanvas = !m_cursorPoint.isNull();
    if (cursorOnCanvas && m_tool == Tool::Brush)
        m_brushTool.renderPreview(*painter, m_doc.planes(), m_doc.background().size(), m_cursorPoint);
    if (m_tool == Tool::CloneStamp) {
        if (cursorOnCanvas)
            m_cloneTool.renderPreview(*painter, cloneSource(), m_doc.planes(),
                                      m_doc.background().size(), m_cursorPoint);
        drawCloneMarker(painter);
    }
    painter->restore();
}

// 仿制源用绿色十字标出，线宽与臂长都按视图缩放换算，屏幕上尺寸恒定
void VpCanvas::drawCloneMarker(QPainter *painter)
{
    if (!m_cloneTool.hasSource())
        return;
    const QPointF marker = m_cloneTool.marker();
    const qreal arm = 7.0 / m_scale;
    painter->save();
    painter->setPen(QPen(QColor("#00e676"), 1.0 / m_scale));
    painter->drawLine(QPointF(marker.x() - arm, marker.y()), QPointF(marker.x() + arm, marker.y()));
    painter->drawLine(QPointF(marker.x(), marker.y() - arm), QPointF(marker.x(), marker.y() + arm));
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
            const int candidateHandle = PerspectivePlane::handleAt(m_doc.planes()[i], point, tolerance);
            if (candidateHandle >= 0) {
                planeIndex = i;
                handle = candidateHandle;
                break;
            }
        }
        // 如果没点到控制点，再检测平面本体
        if (planeIndex < 0)
            planeIndex = PerspectivePlane::planeAt(m_doc.planes(), point);

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
    case Tool::Brush: {
        // 事务要先开，否则撤销基线会带上第一个笔触点
        m_doc.beginPaintTransaction();
        const QRect dirty = m_brushTool.begin(m_doc.paintLayer(), m_doc.planes(),
                                              m_doc.background().size(), point);
        if (!m_brushTool.drawing())
            return; // 没锚定到可绘制面片：事务保持为空，提交时不会记录绘画变更
        m_doc.addPaintDirty(dirty);
        update();
        return;
    }
    case Tool::CloneStamp: {
        // Alt+单击只取源点，不落笔
        if (event->modifiers() & Qt::AltModifier) {
            const bool picked = m_cloneTool.pickSource(m_doc.planes(), m_doc.background().size(), point);
            emit statusMessage(picked ? tr("已设置仿制源，按住 Alt 可重新取样")
                                      : tr("此处无法作为仿制源。"));
            update();
            return;
        }
        if (!m_cloneTool.hasSource()) {
            emit statusMessage(tr("请先按住 Alt 单击，设置仿制源。"));
            return;
        }
        m_doc.beginPaintTransaction();
        const QRect dirty = m_cloneTool.begin(m_doc.paintLayer(), cloneSource(), m_doc.planes(),
                                              m_doc.background().size(), point);
        if (!m_cloneTool.drawing())
            return; // 没锚定到可绘制面片：事务保持为空，提交时不会记录绘画变更
        m_doc.addPaintDirty(dirty);
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
    updateCursorPoint(event->position());
    switch (m_tool) {
    case Tool::EditPlane: {
        if (m_editPlaneIndex < 0)
            return;
        Plane candidate;
        if (!m_editTool.update(m_cursorPoint, &candidate))
            return;
        if (m_editTool.extruding()) {
            // 拉出垂直平面时源平面保持不动，候选几何只作为预览绘制
            m_extrudePreview = candidate;
            m_extrudePreviewReady = true;
        } else {
            m_doc.setPlane(m_editPlaneIndex, candidate);
        }
        update();
        return;
    }
    case Tool::Brush:
        if (m_brushTool.drawing()) {
            m_doc.addPaintDirty(m_brushTool.move(m_doc.paintLayer(), m_cursorPoint));
            update();
        }
        return;
    case Tool::CloneStamp:
        if (m_cloneTool.drawing()) {
            m_doc.addPaintDirty(m_cloneTool.move(m_doc.paintLayer(), m_cursorPoint));
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
    switch (m_tool) {
    case Tool::EditPlane: {
        if (m_editPlaneIndex < 0)
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
        return;
    }
    case Tool::Brush:
        if (m_brushTool.drawing()) {
            m_brushTool.end();
            m_doc.commitHistory(); // 一笔落成，提交为一格历史
            update();
        }
        return;
    case Tool::CloneStamp:
        if (m_cloneTool.drawing()) {
            m_cloneTool.end();
            m_doc.commitHistory(); // 一笔落成，提交为一格历史
            update();
        }
        return;
    default:
        return;
    }
}

// 记录光标位置；仿制源的取样指示与光标预览都依赖它，拖动期间同样要更新。
void VpCanvas::updateCursorPoint(const QPointF &widgetPoint)
{
    m_cursorPoint = widgetToImage(widgetPoint);
    if (m_tool == Tool::CloneStamp)
        m_cloneTool.hover(m_doc.planes(), m_doc.background().size(), m_cursorPoint);
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

    if (!PerspectivePlane::isValidPlane(plane)) {
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
    // 进行中的笔触已经烘焙进绘画层，无法回退，只能提交
    if (m_brushTool.drawing()) {
        m_brushTool.end();
        m_doc.commitHistory();
    }
    if (m_cloneTool.drawing()) {
        m_cloneTool.end();
        m_doc.commitHistory();
    }
    if (m_editPlaneIndex >= 0) {
        m_editPlaneIndex = -1;
        m_doc.cancelEdit();
    }
}

// 是否需要画光标预览（创建平面的橡皮筋、画笔与图章的光标预览）
bool VpCanvas::cursorPreviewVisible() const
{
    if (m_tool == Tool::Brush)
        return true;
    if (m_tool == Tool::CloneStamp)
        return m_cloneTool.hasSource();
    return m_tool == Tool::CreatePlane && m_createTool.creating();
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
