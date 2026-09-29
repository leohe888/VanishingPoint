#include "vpcontroller.h"

#include "core/floatingimageprojection.h"
#include "core/scenerenderer.h"

#include <QClipboard>
#include <QDataStream>
#include <QGuiApplication>
#include <QIODevice>
#include <QPainter>
#include <QLineF>
#include <cmath>

VpController::VpController(QObject *parent) : QObject(parent)
{
    connect(&m_document, &VpDocument::canUndoChanged, this, &VpController::historyChanged);
    connect(&m_document, &VpDocument::canRedoChanged, this, &VpController::historyChanged);
    connect(&m_document, &VpDocument::documentAvailabilityChanged, this, [this] {
        m_cloneTool.resetSource();
        m_cloneSource = QImage();
        m_cloneSourceKey.clear();
        emit documentReplaced();
        emit planeAngleChanged();
        emit repaintRequested();
    });
    connect(&m_document, &VpDocument::imageSelectionChanged, this, [this](bool selected) {
        if (!selected && m_tool == Transform)
            setTool(EditPlane);
        emit repaintRequested();
    });
}

void VpController::setTool(Tool tool)
{
    if (tool == Transform && m_document.selectedFloatingImage() < 0) {
        emit statusMessage(tr("请先选中一张浮动图像（粘贴或框选生成），再使用变换工具。"));
        emit toolChanged();
        return;
    }

    const auto statusForTool = [tool]() {
        switch (tool) {
        case CreatePlane: return tr("依次单击四个角点以创建平面");
        case EditPlane: return tr("拖动内部平移平面，拖动控制点调整形状；Ctrl 从边缘拖出垂直平面；Alt 拖动共享边对边的中点调整夹角");
        case Marquee: return tr("拖动创建透视选区；Shift 正方形；Alt 拖动复制内容；Ctrl 拖动克隆为浮动图像");
        case CloneStamp: return tr("按住 Alt 单击设置仿制源，再在目标位置绘制");
        case Brush: return tr("拖动以绘制笔触");
        case Hand: return tr("拖动以滚动图像");
        case Zoom: return tr("单击放大，按住 Alt 单击缩小");
        case Transform: return tr("拖动控制点缩放，角点外侧拖动旋转；Shift 等比缩放 / 15° 旋转；Alt 中心缩放");
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
    emit planeAngleChanged();
    emit toolChanged();
    emit repaintRequested();
}

void VpController::setBrushDiameter(int value)
{
    const int before = m_brushTool.diameter();
    m_brushTool.setDiameter(value);
    if (before != m_brushTool.diameter())
        emit brushChanged();
}

void VpController::setBrushHardness(int value)
{
    const int before = m_brushTool.hardness();
    m_brushTool.setHardness(value);
    if (before != m_brushTool.hardness())
        emit brushChanged();
}

void VpController::setBrushOpacity(int value)
{
    const int before = m_brushTool.opacity();
    m_brushTool.setOpacity(value);
    if (before != m_brushTool.opacity())
        emit brushChanged();
}

void VpController::setBrushColor(const QColor &color)
{
    if (m_brushTool.color() == color)
        return;
    m_brushTool.setColor(color);
    emit brushChanged();
    emit repaintRequested();
}

void VpController::setCloneDiameter(int value)
{
    const int before = m_cloneTool.diameter();
    m_cloneTool.setDiameter(value);
    if (before != m_cloneTool.diameter())
        emit cloneChanged();
}

void VpController::setCloneHardness(int value)
{
    const int before = m_cloneTool.hardness();
    m_cloneTool.setHardness(value);
    if (before != m_cloneTool.hardness())
        emit cloneChanged();
}

void VpController::setCloneOpacity(int value)
{
    const int before = m_cloneTool.opacity();
    m_cloneTool.setOpacity(value);
    if (before != m_cloneTool.opacity())
        emit cloneChanged();
}

void VpController::setCloneAligned(bool aligned)
{
    if (m_cloneTool.aligned() == aligned)
        return;
    m_cloneTool.setAligned(aligned);
    emit cloneChanged();
    emit repaintRequested();
}

void VpController::setGridSize(int value)
{
    value = qBound(1, value, 1000);
    if (m_gridSize == value)
        return;
    m_gridSize = value;
    emit gridSizeChanged();
    emit repaintRequested();
}

qreal VpController::planeAngle() const
{
    const int index = m_document.selectedPlane();
    if (index < 0 || index >= m_document.planes().size())
        return 90.0;
    return m_document.planes()[index].angleToParentDegrees();
}

bool VpController::canSetSelectedPlaneAngle() const
{
    return planeAngleLockReason().isEmpty();
}

bool VpController::planeAngleEditable() const
{
    return canSetSelectedPlaneAngle();
}

QString VpController::planeAngleLockReason() const
{
    const int index = m_document.selectedPlane();
    if (index < 0 || index >= m_document.planes().size())
        return tr("请先选中一个平面。");
    if (m_document.planes()[index].parentPlaneIndex() < 0)
        return tr("只有从别的平面拖出的子平面才有夹角，当前平面是独立平面。");
    for (const PerspectivePlane &child : m_document.planes()) {
        if (child.parentPlaneIndex() == index && child.hasCustomAngle())
            return tr("它的子平面调整过夹角，父平面角度已锁定，避免整条共享曲面链被重新解释。");
    }
    return {};
}

void VpController::setPlaneAngle(qreal angle)
{
    if (!canSetSelectedPlaneAngle() || !qIsFinite(angle))
        return;
    qreal normalized = std::fmod(angle, 360.0);
    if (normalized == 0 && angle > 0)
        normalized = 360;
    else if (normalized < 0)
        normalized += 360;
    if (normalized == planeAngle())
        return;
    cancelInteraction();
    const int index = m_document.selectedPlane();
    const PerspectivePlane candidate = rotatePlaneAroundEdge(
        m_document.planes()[index], 0, angle, m_document.background().size());
    m_document.beginEdit();
    if (m_document.setPlane(index, candidate)) {
        m_document.commitEdit(true);
        emit planeAngleChanged();
        emit repaintRequested();
    } else {
        m_document.cancelEdit();
    }
}

void VpController::pasteImage()
{
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull()) {
        emit statusMessage(tr("剪贴板中没有可粘贴的图像。"));
        return;
    }
    cancelInteraction();
    m_document.addFloatingImage(image);
    emit focusRequested();
    emit statusMessage(tr("已粘贴图像，按 Delete 键删除。"));
    emit repaintRequested();
}

bool VpController::beginBrush(const QPointF &point)
{
    m_document.beginPaintTransaction();
    const QRect dirty = m_brushTool.begin(m_document.paintLayer(), m_document.planes(),
                                          m_document.background().size(), point);
    if (!m_brushTool.drawing())
        return false;
    m_document.addPaintDirty(dirty);
    return true;
}

void VpController::moveBrush(const QPointF &point)
{
    if (m_brushTool.drawing())
        m_document.addPaintDirty(m_brushTool.move(m_document.paintLayer(), point));
}

void VpController::endBrush()
{
    if (!m_brushTool.drawing())
        return;
    m_brushTool.end();
    m_document.commitHistory();
}

void VpController::renderBrushPreview(QPainter &painter, const QPointF &point) const
{
    m_brushTool.renderPreview(painter, m_document.planes(), m_document.background().size(), point);
}

const QImage &VpController::cloneSource()
{
    if (m_cloneTool.drawing())
        return m_cloneSource;
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << m_document.background().cacheKey() << m_document.paintLayer().cacheKey()
           << qint64(m_document.floatingImages().size());
    for (const FloatingImage &image : m_document.floatingImages())
        stream << image.bitmap.cacheKey() << FloatingImageProjection::cacheKey(image);
    if (key == m_cloneSourceKey && !m_cloneSource.isNull())
        return m_cloneSource;
    m_cloneSourceKey = key;
    m_cloneSource = QImage(m_document.background().size(), QImage::Format_ARGB32_Premultiplied);
    m_cloneSource.fill(Qt::transparent);
    QPainter painter(&m_cloneSource);
    SceneRenderer(m_document).renderContent(painter);
    return m_cloneSource;
}

bool VpController::pickCloneSource(const QPointF &point)
{
    return m_cloneTool.pickSource(m_document.planes(), m_document.background().size(), point);
}

bool VpController::beginClone(const QPointF &point)
{
    if (!m_cloneTool.hasSource())
        return false;
    m_document.beginPaintTransaction();
    const QRect dirty = m_cloneTool.begin(m_document.paintLayer(), cloneSource(),
                                          m_document.planes(), m_document.background().size(), point);
    if (!m_cloneTool.drawing())
        return false;
    m_document.addPaintDirty(dirty);
    return true;
}

void VpController::moveClone(const QPointF &point)
{
    if (m_cloneTool.drawing())
        m_document.addPaintDirty(m_cloneTool.move(m_document.paintLayer(), point));
}

void VpController::endClone()
{
    if (!m_cloneTool.drawing())
        return;
    m_cloneTool.end();
    m_document.commitHistory();
}

void VpController::hoverClone(const QPointF &point)
{
    m_cloneTool.hover(m_document.planes(), m_document.background().size(), point);
}

void VpController::renderClonePreview(QPainter &painter, const QPointF &point)
{
    if (!m_cloneTool.hasSource())
        return;
    m_cloneTool.renderPreview(painter, cloneSource(), m_document.planes(),
                              m_document.background().size(), point);
}

void VpController::finishActiveStrokes()
{
    endBrush();
    endClone();
}

void VpController::pointerPress(const QPointF &point, qreal viewScale, Qt::KeyboardModifiers modifiers)
{
    if (beginFloatingImageInteraction(point, viewScale)) // 浮动图像浮在最上层，先于任何工具处理
        return;
    switch (tool()) {
    case Tool::EditPlane: {
        const qreal tolerance = qMax(8.0 / qMax(viewScale, 1e-6), 4.0);
        int planeIndex = -1;
        int handle = -1;
        // 先检测控制点。后创建的平面在上层，先被检查
        for (int i = m_document.planes().size() - 1; i >= 0; --i) {
            const int candidateHandle = m_document.planes()[i].quad().controlPointIndexAt(point, tolerance);
            if (candidateHandle >= 0 && m_document.planes()[i].controlPointEditable(candidateHandle, modifiers & Qt::ControlModifier)) {
                planeIndex = i;
                handle = candidateHandle;
                break;
            }
        }
        // 如果没点到控制点，再检测平面本体
        if (planeIndex < 0)
            planeIndex = topmostPlaneIndexAt(m_document.planes(), point);

        // 什么都没点到，则取消选择
        if (planeIndex < 0) {
            m_document.setSelectedPlane(-1);
            notifyPlaneAngleChanged(); // 没有选中平面，夹角回到不可调
            emit repaintRequested();
            return;
        }

        // 选中平面
        m_document.setSelectedPlane(planeIndex);
        notifyPlaneAngleChanged();
        // 与相邻平面共边的平面不能整体平移，否则共用边会被撕开
        if (handle < 0 && m_document.isPlaneLinked(planeIndex)) {
            postStatus(tr("该平面已与相邻平面共边，不能整体移动。"));
            emit repaintRequested();
            return;
        }
        const PerspectivePlane &plane = m_document.planes()[planeIndex];
        const int edge = handle >= 4 ? handle - 4 : -1;
        // Ctrl + 拖动边中点：从这条边拖出一个与之垂直的新平面
        const bool extrude = edge >= 0 && (modifiers & Qt::ControlModifier);
        // Alt + 拖动共用边对面的边中点：绕共用边旋转这个子平面，即改它与父平面的夹角
        const bool rotate = (modifiers & Qt::AltModifier) && plane.parentPlaneIndex() >= 0
                            && handle == 4 + 2;
        if (extrude && plane.isEdgeLocked(edge))
            return;
        if (rotate && !planeAngleEditable()) {
            postStatus(planeAngleLockReason());
            return;
        }
        m_editPlaneIndex = planeIndex;
        m_extrudePreviewReady = false;
        m_editTool.begin(plane, point, handle, edge, extrude, m_document.background().size(),
                         rotate, rotate ? 0 : -1);
        m_document.beginEdit();
        if (extrude)
            postStatus(tr("拖动以拉出垂直平面，松开完成。"));
        else if (rotate)
            postStatus(tr("拖动以调整与父平面的夹角，松开完成。"));
        emit repaintRequested();
        return;
    }
    case Tool::CreatePlane:
        m_createTool.addPoint(point);
        if (m_createTool.finished())
            finishPlaneCreation();
        else
            reportCreateProgress();
        emit repaintRequested();
        return;
    case Tool::Brush: {
        if (beginBrush(point))
            emit repaintRequested();
        return;
    }
    case Tool::CloneStamp: {
        // Alt+单击只取源点，不落笔
        if (modifiers & Qt::AltModifier) {
            const bool picked = pickCloneSource(point);
            postStatus(picked ? tr("已设置仿制源，按住 Alt 可重新取样")
                                      : tr("此处无法作为仿制源。"));
            emit repaintRequested();
            return;
        }
        if (!hasCloneSource()) {
            postStatus(tr("请先按住 Alt 单击，设置仿制源。"));
            return;
        }
        if (beginClone(point))
            emit repaintRequested();
        return;
    }
    case Tool::Marquee: {
        QPointF surface;
        const bool insideSelection = m_marqueeTool.contains(point)
                                     && m_marqueeTool.mapToSurface(point, &surface);
        // Alt 拖动：复制内容为浮动图像，并立即接续图像移动。
        if (insideSelection && (modifiers & Qt::AltModifier)) {
            const int index = appendSelectionImage(m_marqueeTool.copy(selectionSampleImage(), point));
            if (index >= 0) {
                m_draggedFloatingImageIndex = index;
                m_document.beginEdit();
                m_floatingImageTransform.beginMove(
                    m_document.floatingImage(index), surface - m_marqueeTool.rect().topLeft());
                m_marqueeTool.clear();
                postStatus(tr("已复制选区内容为浮动图像。"));
            }
            emit repaintRequested();
            return;
        }
        if (insideSelection) {
            if (modifiers & Qt::ControlModifier) {
                const QImage source = selectionSampleImage();
                if (m_marqueeTool.beginFill(point, source, m_document.paintLayer())) {
                    m_document.beginEdit();
                    m_document.beginPaintTransaction();
                    updateSelection(point, modifiers);
                }
            } else {
                m_marqueeTool.beginMove(point);
            }
        } else {
            m_marqueeTool.beginCreate(m_document.planes(), point);
        }
        emit repaintRequested();
        return;
    }
    default:
        return; // 其余工具尚未实现
    }
}
void VpController::pointerMove(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    if (m_marqueeTool.active()) {
        updateSelection(point, modifiers);
        return;
    }
    if (m_draggedFloatingImageIndex >= 0) {
        updateFloatingImageInteraction(point, modifiers);
        return;
    }
    switch (tool()) {
    case Tool::EditPlane: {
        if (m_editPlaneIndex < 0)
            return;
        PerspectivePlane candidate;
        if (!m_editTool.update(point, &candidate))
            return;
        if (m_editTool.extruding()) {
            // 拉出垂直平面时源平面保持不动，候选几何只作为预览绘制
            m_extrudePreview = candidate;
            m_extrudePreviewReady = true;
        } else {
            m_document.setPlane(m_editPlaneIndex, candidate);
            if (m_editTool.rotating())
                notifyPlaneAngleChanged(); // 角度滑杆随拖动实时跟走
        }
        emit repaintRequested();
        return;
    }
    case Tool::Brush:
        if (brushDrawing()) {
            moveBrush(point);
            emit repaintRequested();
        }
        return;
    case Tool::CloneStamp:
        if (cloneDrawing()) {
            moveClone(point);
            emit repaintRequested();
        }
        return;
    default:
        return;
    }
}
void VpController::pointerRelease(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    // 缩放/旋转的最终几何取松开时的位置，避免漏掉最后一次移动；图像拖动
    // 可以在任何工具下进行，所以先于工具分派收尾。
    if (m_draggedFloatingImageIndex >= 0)
        updateFloatingImageInteraction(point, modifiers);
    if (endFloatingImageInteraction())
        return;
    pointerMove(point, modifiers);
    switch (tool()) {
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
        m_document.commitEdit(changed);
        emit repaintRequested();
        return;
    }
    case Tool::Brush:
        if (brushDrawing()) {
            endBrush();
            emit repaintRequested();
        }
        return;
    case Tool::CloneStamp:
        if (cloneDrawing()) {
            endClone();
            emit repaintRequested();
        }
        return;
    case Tool::Marquee: {
        if (!m_marqueeTool.active())
            return;
        updateSelection(point, modifiers);
        if (m_marqueeTool.action() == MarqueeTool::Action::Fill) {
            // 克隆结果不留在绘画层：先丢弃拖动期间的预览像素，再落成一张浮动图像
            m_document.cancelEdit();
            const int index = appendSelectionImage(m_marqueeTool.clone());
            if (index >= 0) {
                m_marqueeTool.clear();
                postStatus(tr("已把拖动结果生成为浮动图像，可直接拖动移动。"));
                emit repaintRequested();
                return;
            }
        }
        // 结束交互并清理取样快照；有效选区继续保留。
        m_marqueeTool.end();
        emit repaintRequested();
        return;
    }
    default:
        return;
    }
}
void VpController::finishPlaneCreation()
{
    const PerspectivePlane plane = m_createTool.makePlane(m_document.nextSurfaceGroupId());
    m_createTool.reset();

    if (!plane.quad().isValid()) {
        postStatus(tr("无法创建：四个点必须依次组成非交叉的凸四边形，请重新设置。"));
        return;
    }
    m_document.beginEdit();
    const int index = m_document.appendPlane(plane);
    m_document.setSelectedPlane(index);
    m_document.commitEdit(true);

    setTool(Tool::EditPlane);
    notifyPlaneAngleChanged(); // 新平面是独立平面，夹角滑杆转为不可调
    postStatus(tr("平面已创建，已自动进入编辑平面工具。"));
}

bool VpController::extrudePlane(int sourcePlane, int edge)
{
    if (!m_extrudePreviewReady)
        return false;
    PerspectivePlane plane = m_extrudePreview;
    // 拖出的面积太小当成误操作，不落盘
    if (!plane.quad().isProjectable())
        return false;
    plane.setParent(sourcePlane, edge); // 父子关系：删除平面时靠它解锁共用边
    plane.setAngleToParentDegrees(90.0);
    plane.setHasCustomAngle(false);
    if (m_document.appendPlane(plane) < 0)
        return false;
    m_document.setSelectedPlane(m_document.planes().size() - 1);
    m_document.lockPlaneEdge(sourcePlane, edge); // 共用边在源平面上不能再编辑
    notifyPlaneAngleChanged(); // 选中项变成新的子平面，夹角滑杆转为可用
    postStatus(tr("已拉出垂直平面。"));
    return true;
}

void VpController::cancelInteraction()
{
    m_createTool.reset();
    m_extrudePreviewReady = false; // 拖出垂直平面的预览随交互一起作废
    // 进行中的笔触已经烘焙进绘画层，切换工具时提交。
    finishActiveStrokes();
    // 平面拖动、图像拖动与 Ctrl 克隆都只改了结构或绘画预览，可以直接丢弃
    if (m_editPlaneIndex >= 0 || m_draggedFloatingImageIndex >= 0
        || m_marqueeTool.active()) {
        m_editPlaneIndex = -1;
        m_floatingImageTransform.reset();
        m_draggedFloatingImageIndex = -1;

        m_document.cancelEdit();
    }
    m_marqueeTool.clear();
}

void VpController::deleteSelectedPlane()
{
    // 拖动编辑进行中：先丢弃这次拖动，否则后续鼠标事件会写回已经移位的平面下标
    if (m_editPlaneIndex >= 0)
        cancelInteraction();
    const int index = m_document.selectedPlane();
    if (index < 0)
        return;
    m_document.removePlane(index);
    notifyPlaneAngleChanged(); // 删除会改变选中项，也可能解开上一级父平面的夹角锁定
    postStatus(tr("已删除选中的平面。"));
    emit repaintRequested();
}

void VpController::reportCreateProgress()
{
    const int count = m_createTool.points().size();
    if (count == 0)
        postStatus(tr("已回退全部角点，请重新点击"));
    else
        postStatus(tr("已设置 %1/%2 个角点").arg(count).arg(PlaneCreateTool::CornerCount));
}

bool VpController::cursorPreviewVisible() const
{
    if (tool() == Tool::Brush)
        return true;
    if (tool() == Tool::CloneStamp)
        return hasCloneSource();
    return tool() == Tool::CreatePlane && m_createTool.creating();
}

bool VpController::beginFloatingImageInteraction(const QPointF &point, qreal viewScale)
{
    if (tool() == Tool::Transform && m_document.selectedFloatingImage() >= 0) {
        const FloatingImage &image = m_document.floatingImage(m_document.selectedFloatingImage());
        const int handle = FloatingImageTransformTool::handleAt(image, point, viewScale);
        const int corner = handle < 0
            ? FloatingImageTransformTool::rotationCornerAt(image, point, viewScale) : -1;
        const auto mode = corner >= 0 ? FloatingImageTransformTool::Mode::Rotate
                                      : FloatingImageTransformTool::Mode::Scale;
        if ((handle >= 0 || corner >= 0)
            && m_floatingImageTransform.beginTransform(
                image, point, corner >= 0 ? corner : handle, mode)) {
            m_draggedFloatingImageIndex = m_document.selectedFloatingImage();
            m_document.beginEdit();
            emit repaintRequested();
            return true;
        }
    }

    QPointF grabOffset;
    int grabbed = -1;
    const bool hitImage = m_document.background().rect().contains(point.toPoint())
                          && floatingImageAt(point, &grabbed, &grabOffset);
    if (!hitImage && m_document.selectedFloatingImage() >= 0) {
        // 点到别处：把选中的图像烘焙进绘画层，之后不再是可操作对象。
        // 这一下点击只用于确认烘焙，不再触发工具的其它动作，避免误落一笔。
        bakeSelectedFloatingImage();
        return true;
    }
    if (hitImage) {
        // 选框工具下只有已选中的图像才拦截点击；点到未选中的图像留给选区建立，
        // 否则贴过图的平面上就再也拖不出新选区。
        if (tool() == Tool::Marquee && grabbed != m_document.selectedFloatingImage())
            return false;
        m_draggedFloatingImageIndex = grabbed;
        m_document.setSelectedFloatingImage(grabbed);
        m_document.beginEdit();
        m_floatingImageTransform.beginMove(m_document.floatingImage(grabbed), grabOffset);
        emit repaintRequested();
        return true;
    }
    return false;
}

void VpController::updateFloatingImageInteraction(const QPointF &point,
                                              Qt::KeyboardModifiers modifiers)
{
    if (m_draggedFloatingImageIndex < 0 || m_draggedFloatingImageIndex >= m_document.floatingImages().size()) {
        cancelInteraction();
        return;
    }
    if (m_floatingImageTransform.isTransforming()) {
        FloatingImage image;
        if (m_floatingImageTransform.update(
                point, modifiers & Qt::ShiftModifier, modifiers & Qt::AltModifier, &image)) {
            m_document.setFloatingImage(m_draggedFloatingImageIndex, image);
        }
        emit repaintRequested();
        return;
    }
    const FloatingImage &start = m_floatingImageTransform.startImage();
    if (tool() == Tool::Transform && start.surfaceAttached) {
        QPointF surface;
        if (start.mapCanvasToPlacement(point, &surface)) {
            m_document.setFloatingImageOrigin(
                m_draggedFloatingImageIndex,
                surface - m_floatingImageTransform.grabOffset());
        }
    } else if (const int plane = topmostPlaneIndexAt(m_document.planes(), point); plane >= 0) {
        attachFloatingImageToPlane(m_draggedFloatingImageIndex, plane, point);

    } else if (!m_document.floatingImage(m_draggedFloatingImageIndex).surfaceAttached
               || !moveSurfaceAttachedImage(m_draggedFloatingImageIndex, point)) {
        m_document.detachFloatingImage(
            m_draggedFloatingImageIndex, point - m_floatingImageTransform.grabOffset());

    }
    emit repaintRequested();
}

bool VpController::endFloatingImageInteraction()
{
    if (m_draggedFloatingImageIndex < 0)
        return false;
    m_floatingImageTransform.reset();
    m_draggedFloatingImageIndex = -1;
    m_document.commitEdit(true); // 模型按实际状态比较，空操作不占历史

    emit repaintRequested();
    return true;
}

bool VpController::floatingImageAt(const QPointF &point, int *index, QPointF *grabOffset) const
{
    for (int i = m_document.floatingImages().size() - 1; i >= 0; --i) {
        if (FloatingImageProjection::forImage(m_document.floatingImage(i))->hitTest(point, grabOffset)) {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

void VpController::attachFloatingImageToPlane(int index, int planeIndex, const QPointF &point)
{
    const PerspectivePlane &host = m_document.planes()[planeIndex];
    QVector<PerspectiveQuad> surfaceQuads;
    int hostQuadIndex = -1;
    for (int i = 0; i < m_document.planes().size(); ++i) {
        const PerspectivePlane &plane = m_document.planes()[i];
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
    m_document.attachFloatingImage(
        index, surfaceQuads, hostQuadIndex,
        surfacePoint - m_floatingImageTransform.grabOffset());
}

bool VpController::moveSurfaceAttachedImage(int index, const QPointF &point)
{
    const FloatingImage &image = m_document.floatingImage(index);
    auto moveOnQuad = [this, index, &point](const PerspectiveQuad &quad) {
        QPointF surfacePoint;
        const bool ok = quad.surfaceToCanvasTransform().mapInverse(point, &surfacePoint);
        if (ok)
            m_document.setFloatingImageOrigin(
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

void VpController::bakeSelectedFloatingImage()
{
    if (m_document.selectedFloatingImage() < 0)
        return;
    const int index = m_document.selectedFloatingImage();
    m_document.beginEdit();
    m_document.beginPaintTransaction();
    QPainter painter(&m_document.paintLayer());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QRect dirty;
    for (int i = 0; i <= index; ++i) {
        const FloatingImage &image = m_document.floatingImage(i);
        SceneRenderer(m_document).renderFloatingImage(painter, image);
        dirty = dirty.united(FloatingImageProjection::forImage(image)->canvasOutline()
                            .boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2));
    }
    painter.end();
    m_document.addPaintDirty(dirty.intersected(m_document.paintLayer().rect()));
    for (int i = index; i >= 0; --i)
        m_document.removeFloatingImage(i);
    m_document.commitEdit(true);
    m_floatingImageTransform.reset();
    m_draggedFloatingImageIndex = -1;

    postStatus(tr("浮动图像已合并到绘画层。"));
    emit repaintRequested();
}

QImage VpController::selectionSampleImage() const
{
    QImage source(m_document.background().size(), QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::transparent);
    QPainter painter(&source);
    SceneRenderer(m_document).renderContent(painter);
    painter.end();
    return source;
}

int VpController::appendSelectionImage(const FloatingImage &image)
{
    if (image.bitmap.isNull()) {
        postStatus(tr("无法提取选区：内容为空、尺寸超过 8192、输出超过 64 MiB 或内存不足。"));
        return -1;
    }
    return m_document.addFloatingImageOnSurface(image.bitmap, image.surfaceQuads,
                                          image.hostQuadIndex, image.placementOrigin);
}

void VpController::updateSelection(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    const QRect dirty = m_marqueeTool.update(point, modifiers, gridSize(),
                                           &m_document.paintLayer());
    if (!dirty.isEmpty())
        m_document.addPaintDirty(dirty);
    emit repaintRequested();
}

void VpController::undo()
{
    cancelInteraction();
    const bool changed = m_document.undo();
    notifyPlaneAngleChanged();
    postStatus(changed ? tr("已撤销。") : tr("没有可撤销的操作。"));
    emit repaintRequested();
}

void VpController::redo()
{
    cancelInteraction();
    const bool changed = m_document.redo();
    notifyPlaneAngleChanged();
    postStatus(changed ? tr("已重做。") : tr("没有可重做的操作。"));
    emit repaintRequested();
}

void VpController::deleteSelection()
{
    if (m_createTool.creating()) {
        m_createTool.removeLastPoint();
        reportCreateProgress();
    } else {
        cancelInteraction();
        if (m_document.selectedFloatingImage() >= 0) {
            m_document.removeFloatingImage(m_document.selectedFloatingImage());
            postStatus(tr("已删除选中的图像。"));
        } else {
            deleteSelectedPlane();
        }
    }
    emit repaintRequested();
}

bool VpController::openImage(const QUrl &url)
{
    if (!url.isLocalFile()) {
        postStatus(tr("请选择本地图片文件。"));
        return false;
    }
    cancelInteraction();
    if (!m_document.loadImage(url.toLocalFile())) {
        postStatus(tr("图片打开失败：文件不可读、格式不支持或超过 128 MiB 解码限制。"));
        return false;
    }
    postStatus(tr("图片已打开，请创建平面或开始绘画。"));
    emit focusRequested();
    return true;
}
