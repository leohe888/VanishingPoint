#include "vpcanvas.h"

#include "core/floatingimagemath.h"
#include "core/imagegeometry.h"
#include "core/scenerenderer.h"

#include <QClipboard>
#include <QCursor>
#include <QDataStream>
#include <QGuiApplication>
#include <QFile>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

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

    // 选中浮动图像或存在选区时让虚线跑起来；都没有就什么都不做，避免空转重绘。
    auto *antsTimer = new QTimer(this);
    antsTimer->setInterval(80);
    connect(antsTimer, &QTimer::timeout, this, [this] {
        if (!isVisible() || (m_doc.selectedImage() < 0 && m_selectionRect.isEmpty()))
            return;
        m_antsPhase = (m_antsPhase + 1) % 8;
        update();
    });
    antsTimer->start();

    // 选中消失（烘焙进绘画层、被删除）后变换工具已无从操作，自动退回编辑平面工具；
    // setTool 内部会放弃进行中的交互，toolChanged 负责把 QML 工具栏一起带回去。
    connect(&m_doc, &CanvasDocument::imageSelectionChanged, this, [this](bool selected) {
        if (!selected && m_tool == Tool::Transform)
            setTool(Tool::EditPlane);
        update();
    });

    updateViewTransform();
}

VpCanvas::Tool VpCanvas::tool() const
{
    return m_tool;
}

// 切换工具：放弃进行中的交互，更新光标与提示。
void VpCanvas::setTool(Tool tool)
{
    // 变换工具必须有选中的浮动图像，否则切过去也无从操作。拒绝时补发一次
    // toolChanged：QML 工具栏在点击那一刻就已经点亮了按钮，不补发就会和画布脱节。
    if (tool == Tool::Transform && m_doc.selectedImage() < 0) {
        emit statusMessage(QObject::tr("请先选中一张浮动图像（粘贴或框选生成），再使用变换工具。"));
        emit toolChanged();
        return;
    }

    const auto statusForTool = [tool]() {
        switch (tool) {
        case Tool::CreatePlane: return QObject::tr("依次单击四个角点以创建平面");
        case Tool::EditPlane: return QObject::tr("拖动内部平移平面，拖动控制点调整形状；Ctrl 从边缘拖出垂直平面；Alt 拖动共享边对边的中点调整夹角");
        case Tool::Marquee: return QObject::tr("拖动创建透视选区；Shift 正方形；Alt 拖动复制内容；Ctrl 拖动克隆为浮动图像");
        case Tool::CloneStamp: return QObject::tr("按住 Alt 单击设置仿制源，再在目标位置绘制");
        case Tool::Brush: return QObject::tr("拖动以绘制笔触");
        case Tool::Transform: return QObject::tr("拖动控制点缩放，角点外侧拖动旋转；Shift 等比缩放 / 15° 旋转；Alt 中心缩放");
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
    emit planeAngleChanged(); // 选项栏随工具显隐，夹角一行的可用态要跟着刷
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

int VpCanvas::gridSize() const
{
    return m_gridSize;
}

// 网格既是平面的绘制辅助，也是选区平移时 Shift 吸附的步长，改完必须重绘
void VpCanvas::setGridSize(int value)
{
    value = qBound(1, value, 1000);
    if (m_gridSize == value)
        return;
    m_gridSize = value;
    emit gridSizeChanged();
    update();
}

qreal VpCanvas::planeAngle() const
{
    const int index = m_doc.selectedPlane();
    if (index < 0 || index >= m_doc.planes().size())
        return 90.0;
    return m_doc.planes()[index].relativeAngle;
}

// 只有从别的平面拖出的子平面才有夹角；若它自己又有子平面被手动调过角度，
// 它作为父平面的朝向就已经被锁定，再改会连带重新解释整条共享曲面链。
bool VpCanvas::canSetSelectedPlaneAngle() const
{
    const int index = m_doc.selectedPlane();
    if (index < 0 || index >= m_doc.planes().size() || m_doc.planes()[index].parentPlane < 0)
        return false;
    for (const Plane &child : m_doc.planes()) {
        if (child.parentPlane == index && child.angleAdjusted)
            return false;
    }
    return true;
}

bool VpCanvas::planeAngleEditable() const
{
    return canSetSelectedPlaneAngle();
}

QString VpCanvas::planeAngleLockReason() const
{
    const int index = m_doc.selectedPlane();
    if (index < 0 || index >= m_doc.planes().size())
        return tr("请先选中一个平面。");
    if (m_doc.planes()[index].parentPlane < 0)
        return tr("只有从别的平面拖出的子平面才有夹角，当前平面是独立平面。");
    for (const Plane &child : m_doc.planes()) {
        if (child.parentPlane == index && child.angleAdjusted)
            return tr("它的子平面调整过夹角，父平面角度已锁定，避免整条共享曲面链被重新解释。");
    }
    return QString();
}

// 改夹角 = 绕共用边把子平面转过去再重投影。共用边固定是子平面的第 0 条边
// （makePerpendicularPlane 的约定，见它设的 lockedEdges）。
void VpCanvas::setPlaneAngle(qreal angle)
{
    if (!canSetSelectedPlaneAngle() || !qIsFinite(angle))
        return;
    const int index = m_doc.selectedPlane();
    const Plane candidate = PerspectivePlane::rotateChildPlane(
        m_doc.planes()[index], 0, angle, m_doc.background().size());
    m_doc.beginEdit();
    if (m_doc.setPlane(index, candidate)) {
        m_doc.commitEdit(true);
        emit planeAngleChanged();
        update();
    } else {
        m_doc.cancelEdit();
    }
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
                                /*hoveredPlane*/ -1, m_antsPhase, /*drawContent*/ true,
                                /*gridSize*/ m_gridSize,
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
    drawImageHandles(painter);
    drawSelectionOutline(painter);
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
    if (beginImageInteraction(point)) // 浮动图像浮在最上层，先于任何工具处理
        return;
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
            emit planeAngleChanged(); // 没有选中平面，夹角回到不可调
            update();
            return;
        }

        // 选中平面
        m_doc.setSelectedPlane(planeIndex);
        emit planeAngleChanged();
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
        // Alt + 拖动共用边对面的边中点：绕共用边旋转这个子平面，即改它与父平面的夹角
        const bool rotate = (event->modifiers() & Qt::AltModifier) && plane.parentPlane >= 0
                            && handle == 4 + 2;
        m_editPlaneIndex = planeIndex;
        m_extrudePreviewReady = false;
        m_editTool.begin(plane, point, handle, edge, extrude, m_doc.background().size(),
                         rotate, rotate ? 0 : -1);
        m_doc.beginEdit();
        if (extrude)
            emit statusMessage(tr("拖动以拉出垂直平面，松开完成。"));
        else if (rotate)
            emit statusMessage(tr("拖动以调整与父平面的夹角，松开完成。"));
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
    case Tool::Marquee: {
        QPointF surface;
        const bool insideSelection = !m_selectionRect.isEmpty()
                                     && selectionPath().contains(point)
                                     && pointToSelectionSurface(point, &surface);
        // Alt+拖动：把选区内容复制成浮动图像，并直接进入拖动
        if (insideSelection && (event->modifiers() & Qt::AltModifier)) {
            const int index = copySelectionToFloatingImage(point);
            if (index >= 0) {
                m_draggingImage = index;
                m_doc.beginEdit();
                m_imageTool.beginMove(m_doc.image(index), surface - m_selectionRect.topLeft());
                clearSelection();
                emit statusMessage(tr("已复制选区内容为浮动图像。"));
            }
            update();
            return;
        }
        if (insideSelection) {
            m_selectionPressSurface = surface;
            m_selectionStartRect = m_selectionRect;
            m_selectionAction = (event->modifiers() & Qt::ControlModifier)
                                    ? SelectionAction::Fill : SelectionAction::Move;
            if (m_selectionAction == SelectionAction::Fill) {
                // 取样源 = 按下这一刻的完整画面；绘画层副本用于每帧重建预览
                m_selectionSampleSource = QImage(m_doc.background().size(), QImage::Format_ARGB32_Premultiplied);
                m_selectionSampleSource.fill(Qt::transparent);
                QPainter sourcePainter(&m_selectionSampleSource);
                SceneRenderer(m_doc).render(sourcePainter, 1.0, /*showGuides*/ false);
                sourcePainter.end();
                m_doc.beginEdit();
                m_doc.beginPaintTransaction();
                m_selectionPaintBefore = m_doc.paintLayer();
                fillSelectionFromPoint(point);
            }
            update();
            return;
        }
        // 点到平面外的空白：放弃选区
        const int planeIndex = PerspectivePlane::planeAt(m_doc.planes(), point);
        if (planeIndex < 0) {
            clearSelection();
            update();
            return;
        }
        // 选区记在整组共享曲面上：从任意一个平面起手都能跨越相邻平面
        const Plane &host = m_doc.planes()[planeIndex];
        m_selectionFaces.clear();
        for (const Plane &plane : m_doc.planes()) {
            if (plane.surfaceGroup != host.surfaceGroup)
                continue;
            Facet face;
            for (int c = 0; c < 4; ++c) {
                face.corner[c] = plane.corner[c];
                face.surfaceCorner[c] = plane.surfaceCorner[c];
            }
            m_selectionFaces.append(face);
        }
        bool ok = false;
        m_selectionPressSurface = PerspectivePlane::planeToSurface(host, point, &ok);
        if (!ok) {
            clearSelection();
            return;
        }
        m_selectionRect = QRectF(m_selectionPressSurface, QSizeF());
        m_selectionAction = SelectionAction::Create;
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
    if (m_selectionAction != SelectionAction::None && (event->buttons() & Qt::LeftButton)) {
        updateSelection(m_cursorPoint, event->modifiers());
        return;
    }
    if (m_draggingImage >= 0 && (event->buttons() & Qt::LeftButton)) {
        updateImageInteraction(m_cursorPoint, event->modifiers());
        return;
    }
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
            if (m_editTool.rotating())
                emit planeAngleChanged(); // 角度滑杆随拖动实时跟走
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
    // 缩放/旋转的最终几何取松开时的位置，避免漏掉最后一次移动；图像拖动
    // 可以在任何工具下进行，所以先于工具分派收尾。
    if (m_draggingImage >= 0 && m_imageTool.transforming())
        updateImageInteraction(widgetToImage(event->position()), event->modifiers());
    if (endImageInteraction())
        return;
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
    case Tool::Marquee: {
        if (m_selectionAction == SelectionAction::None)
            return;
        updateSelection(widgetToImage(event->position()), event->modifiers());
        if (m_selectionAction == SelectionAction::Fill) {
            // 克隆结果不留在绘画层：先丢弃拖动期间的预览像素，再落成一张浮动图像
            m_doc.cancelEdit();
            const int index = cloneSelectionToFloatingImage();
            if (index >= 0) {
                clearSelection();
                emit statusMessage(tr("已把拖动结果生成为浮动图像，可直接拖动移动。"));
                update();
                return;
            }
        }
        // 选区本身不写文档，退化成零面积时直接丢弃
        if (m_selectionRect.width() < 1 || m_selectionRect.height() < 1)
            clearSelection();
        m_selectionAction = SelectionAction::None;
        m_selectionSampleSource = QImage();
        m_selectionPaintBefore = QImage();
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

// 删除键按状态分派：创建平面时回退最后一个角点，否则删除选中的浮动图像，
// 没有图像再退到删除选中的平面。
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
    } else if (m_doc.selectedImage() >= 0) {
        m_doc.removeFloatingImage(m_doc.selectedImage());
        emit statusMessage(tr("已删除选中的图像。"));
    } else {
        deleteSelectedPlane();
    }
    update();
    event->accept();
}

// 粘贴剪贴板里的位图。落成浮动图像而不是烘焙进绘画层，才能随平面做透视变换。
void VpCanvas::pasteImage()
{
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull()) {
        emit statusMessage(tr("剪贴板中没有可粘贴的图像。"));
        return;
    }
    m_doc.addFloatingImage(image); // 追加到画布左上角，并自动选中
    forceActiveFocus();            // 接管键盘焦点，随后的 Delete 才能删掉它
    emit statusMessage(tr("已粘贴图像，按 Delete 键删除。"));
    update();
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
    emit planeAngleChanged(); // 新平面是独立平面，夹角滑杆转为不可调
    emit statusMessage(tr("平面已创建，已自动进入编辑平面工具。"));
}

// 把拖动预览落成一个与源平面垂直的新平面；返回是否真的产生了新平面。
bool VpCanvas::extrudePlane(int sourcePlane, int edge)
{
    if (!m_extrudePreviewReady)
        return false;
    Plane plane = m_extrudePreview;
    // 拖出的面积太小当成误操作，不落盘
    const QRectF bounds = PerspectivePlane::planePolygon(plane.corner).boundingRect();
    if (qAbs(bounds.width() * bounds.height()) <= 100.0)
        return false;
    plane.parentPlane = sourcePlane; // 父子关系：删除平面时靠它解锁共用边
    plane.parentEdge = edge;
    plane.relativeAngle = 90.0;      // 新平面与父平面垂直，尚未被手动调过夹角
    plane.angleAdjusted = false;
    if (m_doc.appendPlane(plane) < 0)
        return false;
    m_doc.setSelectedPlane(m_doc.planes().size() - 1);
    m_doc.lockPlaneEdge(sourcePlane, edge); // 共用边在源平面上不能再编辑
    emit planeAngleChanged(); // 选中项变成新的子平面，夹角滑杆转为可用
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
    // 平面拖动、图像拖动与 Ctrl 克隆都只改了结构或绘画预览，可以直接丢弃
    if (m_editPlaneIndex >= 0 || m_draggingImage >= 0
        || m_selectionAction != SelectionAction::None) {
        m_editPlaneIndex = -1;
        m_imageTool.reset();
        m_draggingImage = -1;
        m_imageChanged = false;
        m_doc.cancelEdit();
    }
    clearSelection();
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
    emit planeAngleChanged(); // 删除会改变选中项，也可能解开上一级父平面的夹角锁定
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

// 按下时的图像处理。返回 true 表示这次按下已被图像消费，工具不再响应。
// 顺序与 Photoshop 一致：先试控制点（仅变换工具），再试图像本体，最后才轮到烘焙。
bool VpCanvas::beginImageInteraction(const QPointF &point)
{
    if (m_tool == Tool::Transform && m_doc.selectedImage() >= 0) {
        const FloatingImage &image = m_doc.image(m_doc.selectedImage());
        const int handle = ImageTransformTool::handleAt(image, point, m_scale);
        const int corner = handle < 0 ? ImageTransformTool::rotationCornerAt(image, point, m_scale) : -1;
        const auto mode = corner >= 0 ? ImageTransformTool::Mode::Rotate : ImageTransformTool::Mode::Scale;
        if ((handle >= 0 || corner >= 0) && m_imageTool.begin(image, point, corner >= 0 ? corner : handle, mode)) {
            m_draggingImage = m_doc.selectedImage();
            m_doc.beginEdit();
            update();
            return true;
        }
    }

    QPointF grabOffset;
    int grabbed = -1;
    const bool hitImage = m_doc.background().rect().contains(point.toPoint())
                          && floatingImageAt(point, &grabbed, &grabOffset);
    if (!hitImage && m_doc.selectedImage() >= 0) {
        // 点到别处：把选中的图像烘焙进绘画层，之后不再是可操作对象。
        // 这一下点击只用于确认烘焙，不再触发工具的其它动作，避免误落一笔。
        bakeSelectedImage();
        return true;
    }
    if (hitImage) {
        // 选框工具下只有已选中的图像才拦截点击；点到未选中的图像留给选区建立，
        // 否则贴过图的平面上就再也拖不出新选区。
        if (m_tool == Tool::Marquee && grabbed != m_doc.selectedImage())
            return false;
        m_draggingImage = grabbed;
        m_doc.setSelectedImage(grabbed);
        m_doc.beginEdit();
        m_imageTool.beginMove(m_doc.image(grabbed), grabOffset);
        update();
        return true;
    }
    return false;
}

// 拖动中的图像：缩放/旋转交给工具自己算新几何；平移则沿快照曲面滑动（变换工具下
// 已吸附的图像），或者按光标所在的平面吸附，落在平面外时脱离回画布坐标。
// 越过极点线映射不出来时保持原位。
void VpCanvas::updateImageInteraction(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    if (m_imageTool.transforming()) {
        FloatingImage image;
        if (m_imageTool.update(point, modifiers & Qt::ShiftModifier, modifiers & Qt::AltModifier, &image)) {
            m_doc.setImage(m_draggingImage, image);
            // 与起始几何比对后才算改动：按住控制点原地松手不该占一格历史
            const FloatingImage &start = m_imageTool.start();
            m_imageChanged = image.position != start.position || image.scale != start.scale
                             || image.rotation != start.rotation;
        }
        update();
        return;
    }
    const FloatingImage &start = m_imageTool.start();
    if (m_tool == Tool::Transform && start.attached) {
        QPointF surface;
        if (FloatingImageMath::fromCanvas(start, point, &surface)) {
            m_doc.setImagePosition(m_draggingImage, surface - m_imageTool.grabOffset());
            // 沿曲面滑动可能原地不动（越过极点线时保持原位），不算一次改动
            m_imageChanged = m_doc.image(m_draggingImage).position != start.position;
        }
    } else if (const int plane = PerspectivePlane::planeAt(m_doc.planes(), point); plane >= 0) {
        attachImageToPlane(m_draggingImage, plane, point);
        m_imageChanged = true;
    } else if (!m_doc.image(m_draggingImage).attached || !moveAttachedImage(m_draggingImage, point)) {
        m_doc.detachImage(m_draggingImage, point - m_imageTool.grabOffset());
        m_imageChanged = true;
    }
    update();
}

// 结束拖动并提交；返回 false 表示当时没有图像在拖动。
bool VpCanvas::endImageInteraction()
{
    if (m_draggingImage < 0)
        return false;
    m_imageTool.reset();
    m_draggingImage = -1;
    m_doc.commitEdit(m_imageChanged); // 只是点了一下、几何没变就不占一格历史
    m_imageChanged = false;
    update();
    return true;
}

// 命中测试：判断画布坐标是否落在某张浮动图像上（从最上层开始），
// 命中时返回该点相对图像左上角的抓取偏移。
bool VpCanvas::floatingImageAt(const QPointF &point, int *index, QPointF *grabOffset) const
{
    for (int i = m_doc.images().size() - 1; i >= 0; --i) {
        if (ImageGeometry::get(m_doc.image(i))->hitTest(point, grabOffset)) {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

// 把图像吸附到目标平面所在的曲面分组：拷贝该分组的全部面片几何作为严格快照，
// 此后平面的增删改都不再影响它。
void VpCanvas::attachImageToPlane(int index, int planeIndex, const QPointF &point)
{
    const Plane &host = m_doc.planes()[planeIndex];
    QVector<Facet> faces;
    int hostFace = -1;
    for (int i = 0; i < m_doc.planes().size(); ++i) {
        const Plane &plane = m_doc.planes()[i];
        if (plane.surfaceGroup != host.surfaceGroup)
            continue;
        Facet face;
        for (int c = 0; c < 4; ++c) {
            face.corner[c] = plane.corner[c];
            face.surfaceCorner[c] = plane.surfaceCorner[c];
        }
        if (i == planeIndex)
            hostFace = faces.size();
        faces.append(face);
    }
    bool ok = false;
    const QPointF surfacePoint = PerspectivePlane::planeToSurface(host, point, &ok);
    if (!ok)
        return;
    m_doc.attachImage(index, faces, hostFace, surfacePoint - m_imageTool.grabOffset());
}

// 已吸附的图像沿它的快照曲面移动：优先用包住光标的那个面片，都不包住时退回宿主面片
// 外推；连外推都映射不出来（越过极点线）才返回 false。
bool VpCanvas::moveAttachedImage(int index, const QPointF &point)
{
    const FloatingImage &image = m_doc.image(index);
    auto moveOnFace = [this, index, &point](const Facet &face) {
        bool ok = false;
        const QPointF surfacePoint = PerspectivePlane::planeToSurface(face, point, &ok);
        if (ok)
            m_doc.setImagePosition(index, surfacePoint - m_imageTool.grabOffset());
        return ok;
    };
    for (auto face = image.faces.crbegin(); face != image.faces.crend(); ++face) {
        if (PerspectivePlane::planePolygon(face->corner).containsPoint(point, Qt::OddEvenFill)
            && moveOnFace(*face))
            return true;
    }
    return image.hostFace >= 0 && image.hostFace < image.faces.size()
           && moveOnFace(image.faces[image.hostFace]);
}

// 把当前选中的浮动图像按当前几何画进绘画层并删除它：走的是与屏幕渲染完全相同的
// 分段投影路径，因此肉眼看不到像素跳变。烘焙后内容并入绘画层，不再能单独操作。
void VpCanvas::bakeSelectedImage()
{
    if (m_doc.selectedImage() < 0)
        return;
    const int index = m_doc.selectedImage();
    const FloatingImage image = m_doc.image(index); // 拷贝：移除后仍要用它的几何算脏矩形
    m_doc.beginPaintTransaction();
    QPainter painter(&m_doc.paintLayer());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    SceneRenderer(m_doc).renderFloatingImage(painter, image);
    painter.end();
    const QRect dirty = ImageGeometry::get(image)->outline().boundingRect().toAlignedRect()
                            .adjusted(-2, -2, 2, 2).intersected(m_doc.paintLayer().rect());
    m_doc.addPaintDirty(dirty);
    // 删除会顺带取消选中，并在同一步历史里记录结构变化与绘画层增量，撤销时一起回退。
    m_doc.removeFloatingImage(index);
    m_imageTool.reset();
    m_draggingImage = -1;
    m_imageChanged = false;
    emit statusMessage(tr("浮动图像已合并到绘画层。"));
    update();
}

// 变换工具下画出浮动图像的 8 个控制点；尺寸与线宽按视图缩放换算，屏幕上恒定。
void VpCanvas::drawImageHandles(QPainter *painter)
{
    if (m_tool != Tool::Transform || m_doc.selectedImage() < 0)
        return;
    const auto geometry = ImageGeometry::get(m_doc.image(m_doc.selectedImage()));
    const qreal half = 4.0 / m_scale;
    painter->save();
    painter->setPen(QPen(QColor("#1769aa"), 1.0 / m_scale));
    painter->setBrush(Qt::white);
    for (const QPointF &control : geometry->controls())
        painter->drawRect(QRectF(control.x() - half, control.y() - half, half * 2, half * 2));
    painter->restore();
}

// 选区状态整体归零：只在工具切走、交互被取消、或选区退化成零面积时调用。
// 因为选区本身不写文档，清空它不产生历史。
void VpCanvas::clearSelection()
{
    m_selectionFaces.clear();
    m_selectionRect = QRectF();
    m_selectionStartRect = QRectF();
    m_selectionAction = SelectionAction::None;
    m_selectionSampleSource = QImage();
    m_selectionPaintBefore = QImage();
}

// 把画面上的点换算到选区所在曲面的展开坐标：优先用包住它的那个面片，
// 都不包住时退回第一个面片外推。选区因此可以跨越共享曲面的多个平面。
bool VpCanvas::pointToSelectionSurface(const QPointF &point, QPointF *surface) const
{
    if (!surface || m_selectionFaces.isEmpty())
        return false;
    for (int i = m_selectionFaces.size() - 1; i >= 0; --i) {
        const Facet &face = m_selectionFaces[i];
        if (!PerspectivePlane::planePolygon(face.corner).containsPoint(point, Qt::OddEvenFill))
            continue;
        bool ok = false;
        *surface = PerspectivePlane::planeToSurface(face, point, &ok);
        if (ok)
            return true;
    }
    bool ok = false;
    *surface = PerspectivePlane::planeToSurface(m_selectionFaces.first(), point, &ok);
    return ok;
}

// 选区轮廓：把展开坐标下的矩形按面片切开再逐片投影回画面，
// 于是跨接缝的选区画出来会沿折线拐弯，而不是一个平面矩形。
QPainterPath VpCanvas::selectionPath() const
{
    QPainterPath result;
    if (m_selectionRect.isEmpty())
        return result;
    QPainterPath rectangle;
    rectangle.addRect(m_selectionRect.normalized());
    for (const Facet &face : m_selectionFaces) {
        QPainterPath facePath;
        facePath.addPolygon(PerspectivePlane::planePolygon(face.surfaceCorner));
        facePath.closeSubpath();
        const QPainterPath clipped = rectangle.intersected(facePath);
        const PerspectiveTransform mapping = PerspectivePlane::surfaceMapping(face);
        if (!mapping.isValid())
            continue;
        for (const QPolygonF &surfacePolygon : clipped.toFillPolygons()) {
            QPolygonF canvasPolygon;
            for (const QPointF &surfacePoint : surfacePolygon) {
                QPointF canvasPoint;
                if (mapping.mapForward(surfacePoint, &canvasPoint))
                    canvasPolygon.append(canvasPoint);
            }
            if (canvasPolygon.size() >= 3) {
                QPainterPath patch;
                patch.addPolygon(canvasPolygon);
                patch.closeSubpath();
                result = result.united(patch);
            }
        }
    }
    return result;
}

// 拖动过程中的选区：新建时从按下点拉到光标（Shift 取正方形），平移时整体位移
// （Shift 锁单轴并对齐网格），Ctrl 克隆则持续把光标处的内容补进选区。
void VpCanvas::updateSelection(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    QPointF surface;
    if (!pointToSelectionSurface(point, &surface))
        return;
    if (m_selectionAction == SelectionAction::Create) {
        QPointF delta = surface - m_selectionPressSurface;
        if (modifiers & Qt::ShiftModifier) {
            const qreal side = qMax(qAbs(delta.x()), qAbs(delta.y()));
            delta.setX(delta.x() < 0 ? -side : side);
            delta.setY(delta.y() < 0 ? -side : side);
        }
        m_selectionRect = QRectF(m_selectionPressSurface,
                                 m_selectionPressSurface + delta).normalized();
    } else if (m_selectionAction == SelectionAction::Move) {
        QPointF delta = surface - m_selectionPressSurface;
        if (modifiers & Qt::ShiftModifier) {
            // 锁到位移较大的那个轴，再吸附到网格
            if (qAbs(delta.x()) >= qAbs(delta.y()))
                delta.setY(0);
            else
                delta.setX(0);
            delta.setX(qRound(delta.x() / m_gridSize) * m_gridSize);
            delta.setY(qRound(delta.y() / m_gridSize) * m_gridSize);
        }
        m_selectionRect = m_selectionStartRect.translated(delta);
    } else if (m_selectionAction == SelectionAction::Fill) {
        fillSelectionFromPoint(point);
    }
    update();
}

// Ctrl 克隆：把「按下时光标下方的区域」按单应搬进选区。每帧都从按下前的绘画层
// 重建，避免重复移动把上一次的预览也当成取样内容叠上去。
void VpCanvas::fillSelectionFromPoint(const QPointF &point)
{
    if (m_selectionSampleSource.isNull() || m_selectionPaintBefore.isNull())
        return;

    QPointF sourceAnchor;
    if (!pointToSelectionSurface(point, &sourceAnchor))
        return;
    const QPointF sourceOffset = sourceAnchor - m_selectionPressSurface;
    m_selectionFillOffset = sourceOffset;
    const QPainterPath targetPath = selectionPath();
    const QRect dirty = targetPath.boundingRect().toAlignedRect().adjusted(-1, -1, 1, 1)
                            .intersected(m_doc.paintLayer().rect());
    if (dirty.isEmpty())
        return;

    m_doc.paintLayer() = m_selectionPaintBefore;
    QPainter painter(&m_doc.paintLayer());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QPainterPath selectionSurfacePath;
    selectionSurfacePath.addRect(m_selectionRect.normalized());
    // 每个「目标面 + 源面」组合是一片单应补丁，整片交给光栅器一次画完。
    for (const Facet &targetFace : m_selectionFaces) {
        QPainterPath targetSurfacePath;
        targetSurfacePath.addPolygon(PerspectivePlane::planePolygon(targetFace.surfaceCorner));
        targetSurfacePath.closeSubpath();
        const PerspectiveTransform targetMapping = PerspectivePlane::surfaceMapping(targetFace);
        if (!targetMapping.isValid())
            continue;
        for (const Facet &sourceFace : m_selectionFaces) {
            QPolygonF shiftedSourceSurface;
            QPolygonF sourceCanvas;
            QPolygonF targetCanvas;
            for (int c = 0; c < 4; ++c) {
                shiftedSourceSurface.append(sourceFace.surfaceCorner[c] - sourceOffset);
                sourceCanvas.append(sourceFace.corner[c]);
                QPointF mapped;
                if (!targetMapping.mapForward(shiftedSourceSurface.last(), &mapped)) {
                    targetCanvas.clear();
                    break;
                }
                targetCanvas.append(mapped);
            }
            if (targetCanvas.size() != 4)
                continue;

            QPainterPath sourceDomain;
            sourceDomain.addPolygon(shiftedSourceSurface);
            sourceDomain.closeSubpath();
            const QPainterPath surfacePatch = selectionSurfacePath
                                                  .intersected(targetSurfacePath)
                                                  .intersected(sourceDomain);
            if (surfacePatch.isEmpty())
                continue;

            QPainterPath canvasClip;
            for (const QPolygonF &polygon : surfacePatch.toFillPolygons()) {
                QPolygonF mappedPolygon;
                for (const QPointF &surfacePoint : polygon) {
                    QPointF mapped;
                    if (targetMapping.mapForward(surfacePoint, &mapped))
                        mappedPolygon.append(mapped);
                }
                if (mappedPolygon.size() >= 3) {
                    canvasClip.addPolygon(mappedPolygon);
                    canvasClip.closeSubpath();
                }
            }
            const PerspectiveTransform sourceToTarget(sourceCanvas, targetCanvas);
            if (!sourceToTarget.isValid() || canvasClip.isEmpty())
                continue;
            painter.save();
            painter.setClipPath(canvasClip, Qt::IntersectClip);
            painter.setWorldTransform(sourceToTarget.forward());
            painter.drawImage(QPointF(), m_selectionSampleSource);
            painter.restore();
        }
    }
    painter.end();
    m_doc.addPaintDirty(dirty);
}

// Alt 拖动：把选区里的内容（背景 + 绘画层 + 浮动图像）抠成一张浮动图像，
// 并吸附到选区所在的同一组面片上，于是它还能继续被拖动、缩放。
int VpCanvas::copySelectionToFloatingImage(const QPointF &point)
{
    const QRectF rect = m_selectionRect.normalized();
    if (rect.width() < 1 || rect.height() < 1 || m_selectionFaces.isEmpty())
        return -1;
    const QSize size(qMin(8192, qMax(1, qCeil(rect.width()))),
                     qMin(8192, qMax(1, qCeil(rect.height()))));
    QImage source(m_doc.background().size(), QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::transparent);
    {
        QPainter sourcePainter(&source);
        SceneRenderer(m_doc).render(sourcePainter, 1.0, /*showGuides*/ false);
    }
    QImage extracted(size, QImage::Format_ARGB32_Premultiplied);
    extracted.fill(Qt::transparent);
    QPainter painter(&extracted);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    int hostFace = 0;
    QPointF pressSurface;
    pointToSelectionSurface(point, &pressSurface);
    for (int i = 0; i < m_selectionFaces.size(); ++i) {
        const Facet &face = m_selectionFaces[i];
        QPolygonF target;
        for (int c = 0; c < 4; ++c)
            target.append(face.surfaceCorner[c] - rect.topLeft());
        QPainterPath clip;
        clip.addPolygon(target);
        clip.closeSubpath();
        QPainterPath outputBounds;
        outputBounds.addRect(QRectF(QPointF(), QSizeF(size)));
        clip = outputBounds.intersected(clip);
        const PerspectiveTransform mapping(PerspectivePlane::planePolygon(face.corner), target);
        if (!mapping.isValid() || clip.isEmpty())
            continue;
        painter.save();
        painter.setClipPath(clip);
        painter.setWorldTransform(mapping.forward());
        painter.drawImage(QPointF(), source);
        painter.restore();
        if (PerspectivePlane::planePolygon(face.surfaceCorner).containsPoint(pressSurface, Qt::OddEvenFill))
            hostFace = i;
    }
    painter.end();
    return m_doc.addFloatingImageOnSurface(extracted, m_selectionFaces, hostFace, rect.topLeft());
}

// Ctrl 拖动的结果不留在绘画层，而是落成一张浮动图像：与 fillSelectionFromPoint
// 共用同一套「源面 → 目标面」单应，因此松手前后看到的像素不会跳变。
int VpCanvas::cloneSelectionToFloatingImage()
{
    const QRectF rect = m_selectionRect.normalized();
    if (m_selectionSampleSource.isNull() || m_selectionFaces.isEmpty()
        || rect.width() < 1 || rect.height() < 1) {
        return -1;
    }
    const QSize size(qMin(8192, qMax(1, qCeil(rect.width()))),
                     qMin(8192, qMax(1, qCeil(rect.height()))));
    QImage extracted(size, QImage::Format_ARGB32_Premultiplied);
    extracted.fill(Qt::transparent);
    QPainter painter(&extracted);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const QPointF origin = rect.topLeft();
    QPainterPath bounds; // 位图范围，等价于选区矩形
    bounds.addRect(QRectF(QPointF(), QSizeF(size)));
    int hostFace = 0;
    for (int i = 0; i < m_selectionFaces.size(); ++i) {
        if (PerspectivePlane::planePolygon(m_selectionFaces[i].surfaceCorner)
                .containsPoint(m_selectionPressSurface, Qt::OddEvenFill)) {
            hostFace = i;
        }
    }
    for (const Facet &targetFace : m_selectionFaces) {
        QPolygonF targetQuad;
        for (int c = 0; c < 4; ++c)
            targetQuad.append(targetFace.surfaceCorner[c] - origin);
        QPainterPath targetSurface;
        targetSurface.addPolygon(targetQuad);
        targetSurface.closeSubpath();
        for (const Facet &sourceFace : m_selectionFaces) {
            QPolygonF shifted; // 源面按拖动偏移搬到目标位置后，在位图中的四边形
            for (int c = 0; c < 4; ++c)
                shifted.append(sourceFace.surfaceCorner[c] - m_selectionFillOffset - origin);
            QPainterPath sourceDomain;
            sourceDomain.addPolygon(shifted);
            sourceDomain.closeSubpath();
            const QPainterPath clip = bounds.intersected(targetSurface).intersected(sourceDomain);
            if (clip.isEmpty())
                continue;
            QPolygonF sourceCanvas;
            for (int c = 0; c < 4; ++c)
                sourceCanvas.append(sourceFace.corner[c]);
            const PerspectiveTransform mapping(sourceCanvas, shifted);
            if (!mapping.isValid())
                continue;
            painter.save();
            painter.setClipPath(clip);
            painter.setWorldTransform(mapping.forward());
            painter.drawImage(QPointF(), m_selectionSampleSource);
            painter.restore();
        }
    }
    painter.end();
    return m_doc.addFloatingImageOnSurface(extracted, m_selectionFaces, hostFace, rect.topLeft());
}

// 选区轮廓按控件坐标描边（先 resetTransform 再整体缩放），
// 白 1px 打底 + 黑虚线，线宽与虚线间距在屏幕上恒定。
void VpCanvas::drawSelectionOutline(QPainter *painter)
{
    if (m_tool != Tool::Marquee || m_selectionRect.isEmpty())
        return;
    painter->save();
    painter->resetTransform();
    const QPainterPath outline =
        QTransform::fromTranslate(m_offset.x(), m_offset.y())
            .map(QTransform::fromScale(m_scale, m_scale).map(selectionPath()));
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
