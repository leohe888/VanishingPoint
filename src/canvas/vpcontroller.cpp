#include "vpcontroller.h"

#include "../core/floatingselectionprojection.h"
#include "../core/scenerenderer.h"

#include <QDataStream>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QIODevice>
#include <QPainter>
#include <QLineF>
#include <cmath>

VpController::VpController(QObject *parent) : QObject(parent)
{
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
            m_preferInternalCopy = false; // A newer external copy supersedes the internal snapshot.
        });
    }
    connect(&m_document, &VpDocument::structureChanged, this, &VpController::marqueeMoveModeChanged);
    connect(&m_document, &VpDocument::floatingSelectionChanged, this, &VpController::marqueeMoveModeChanged);
    connect(&m_document, &VpDocument::canUndoChanged, this, &VpController::historyChanged);
    connect(&m_document, &VpDocument::canRedoChanged, this, &VpController::historyChanged);
    connect(&m_document, &VpDocument::structureChanged, this, &VpController::refreshToolAvailability);
    connect(&m_document, &VpDocument::structureChanged, this, &VpController::transformOptionsChanged);
    connect(&m_document, &VpDocument::structureChanged, this, &VpController::syncMarqueeOptions);
    connect(&m_document, &VpDocument::documentAvailabilityChanged, this, [this] {
        m_sourceImageUrl = QUrl();
        if (!m_savedImageUrl.isEmpty()) {
            m_savedImageUrl = QUrl();
            emit savedImageUrlChanged();
        }
        setInteractionHint(QString());
        emit statusMessageCleared();
        m_ctrlPlaneCyclePending = false;
        m_marqueeTool.clear();
        resetMarqueeMoveMode();
        m_cloneTool.resetSource();
        m_cloneSource = QImage();
        m_sampleImage = QImage();
        m_sampleKey.clear();
        m_copiedSelection = FloatingSelection();
        m_preferInternalCopy = false;
        emit documentReplaced();
        emit planeAngleChanged();
        emit repaintRequested();
    });
    connect(&m_document, &VpDocument::floatingSelectionChanged, this, [this] {
        syncMarqueeOptions();
        emit transformOptionsChanged();
        emit repaintRequested();
    });
}

bool VpController::transformEditable() const
{
    return m_document.selectedFloatingSelection() >= 0;
}

bool VpController::flippedHorizontally() const
{
    return transformEditable()
        && m_document.floatingSelection(m_document.selectedFloatingSelection()).flippedHorizontally;
}

bool VpController::flippedVertically() const
{
    return transformEditable()
        && m_document.floatingSelection(m_document.selectedFloatingSelection()).flippedVertically;
}

void VpController::setFlippedHorizontally(bool flipped)
{
    setSelectionFlipped(true, flipped);
}

void VpController::setFlippedVertically(bool flipped)
{
    setSelectionFlipped(false, flipped);
}

void VpController::setSelectionFlipped(bool horizontal, bool flipped)
{
    if (m_tool != VpTools::Transform || !transformEditable())
        return;
    cancelInteraction(false);
    const int index = m_document.selectedFloatingSelection();
    if (index < 0)
        return;
    FloatingSelection selection = m_document.floatingSelection(index);
    bool &state = horizontal ? selection.flippedHorizontally : selection.flippedVertically;
    if (state == flipped)
        return;
    // 翻转像素保留现有控制点顺序、缩放、旋转和曲面吸附几何。
    selection.bitmap = selection.bitmap.mirrored(horizontal, !horizontal);
    if (!selection.unhealedBitmap.isNull())
        selection.unhealedBitmap = selection.unhealedBitmap.mirrored(horizontal, !horizontal);
    if (!selection.unfeatheredBitmap.isNull())
        selection.unfeatheredBitmap = selection.unfeatheredBitmap.mirrored(horizontal, !horizontal);
    state = flipped;
    m_document.beginEdit();
    const bool changed = m_document.setFloatingSelection(index, selection);
    m_document.commitEdit(changed);
    emit repaintRequested();
}

int VpController::availableTools() const
{
    int tools = (1 << VpTools::CreatePlane) | (1 << VpTools::Hand) | (1 << VpTools::Zoom);
    if (m_document.hasLoadedImage())
        tools |= 1 << VpTools::Eyedropper;
    if (!m_document.planes().isEmpty()) tools |= 1 << VpTools::EditPlane;
    for (const PerspectivePlane &plane : m_document.planes())
        if (plane.quad().isProjectable()) {
            tools |= (1 << VpTools::Marquee) | (1 << VpTools::CloneStamp) | (1 << VpTools::Brush);
            break;
        }
    if (!m_document.floatingSelections().isEmpty())
        tools |= (1 << VpTools::Transform) | (1 << VpTools::Marquee);
    return tools;
}

bool VpController::isToolEnabled(VpTools::Tool tool) const
{
    return tool >= VpTools::CreatePlane && tool <= VpTools::Eyedropper && (availableTools() & (1 << tool));
}

void VpController::refreshToolAvailability()
{
    const int tools = availableTools();
    if (tools != m_availableTools) {
        m_availableTools = tools;
        emit toolAvailabilityChanged();
    }
    // 等结构事务结束再切换，避免取消正在烘焙或删除的内容。
    if (!m_document.editActive() && !isToolEnabled(m_tool))
        setTool(isToolEnabled(VpTools::EditPlane) ? VpTools::EditPlane : VpTools::CreatePlane);
}

QString VpController::toolHint() const
{
    switch (m_tool) {
    case VpTools::CreatePlane: return tr("依次单击四个角点以创建平面");
    case VpTools::EditPlane: return tr("拖动内部平移平面，拖动控制点调整形状；Ctrl 从边缘拖出垂直平面；Alt 拖动共享边对边的中点调整夹角");
    case VpTools::Marquee: return tr("拖动创建透视选区；Shift 正方形；Alt 拖动复制内容；Ctrl 拖动克隆为浮动选区");
    case VpTools::CloneStamp: return tr("按住 Alt 单击设置仿制源，再在目标位置绘制");
    case VpTools::Brush: return tr("拖动以绘制笔触");
    case VpTools::Eyedropper: return tr("单击或拖动取样画面颜色，设置画笔颜色");
    case VpTools::Hand: return tr("拖动以滚动图像");
    case VpTools::Zoom: return tr("单击放大，按住 Alt 单击缩小");
    case VpTools::Transform: return tr("拖动控制点缩放，角点外侧拖动旋转；Shift 等比缩放 / 15° 旋转；Alt 中心缩放");
    }
    return QString();
}

void VpController::setTool(VpTools::Tool tool)
{
    if (!isToolEnabled(tool)) {
        emit statusMessage(tool == VpTools::Transform
            ? tr("请先粘贴或框选生成浮动选区，再使用变换工具。")
            : tr("请先创建平面，再使用此工具。"), true);
        emit toolChanged();
        return;
    }

    if (tool == m_tool)
        return;
    const auto hasContentOverlay = [](VpTools::Tool value) {
        return value == VpTools::Brush || value == VpTools::CloneStamp
            || value == VpTools::Transform;
    };
    const bool contentOverlayChanged = hasContentOverlay(m_tool) || hasContentOverlay(tool)
        || m_draggedFloatingSelectionIndex >= 0 || m_marqueeTool.action() == MarqueeTool::Action::Fill;
    cancelInteraction(false);
    if (!isToolEnabled(tool))
        return;
    if (tool == VpTools::Transform && m_document.selectedFloatingSelection() < 0)
        m_document.setSelectedFloatingSelection(m_document.floatingSelections().size() - 1);
    if (tool == VpTools::Marquee || tool == VpTools::Brush || tool == VpTools::CloneStamp)
        m_document.setSelectedPlane(-1);
    m_tool = tool;
    emit marqueeMoveModeChanged();
    syncMarqueeOptions();
    emit planeAngleChanged();
    emit toolChanged();
    if (contentOverlayChanged)
        emit repaintRequested();
    else
        emit guidesRepaintRequested();
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

bool VpController::pickBrushColor(const QPointF &point)
{
    const QSize size = m_document.background().size();
    if (!qIsFinite(point.x()) || !qIsFinite(point.y())
        || point.x() < 0 || point.y() < 0 || point.x() >= size.width() || point.y() >= size.height())
        return false;
    const QImage source = selectionSampleImage();
    if (source.isNull()) return false;
    QColor color = source.pixelColor(qFloor(point.x()), qFloor(point.y()));
    if (color.alpha() == 0) return false;
    // 颜色选项与画笔都使用不带 alpha 的颜色，不把取样透明度当作画笔不透明度。
    color.setAlpha(255);
    setBrushColor(color);
    return true;
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
    emit guidesRepaintRequested();
}

qreal VpController::planeAngle() const
{
    const int index = m_document.selectedPlane();
    if (index < 0 || index >= m_document.planes().size())
        return 90.0;
    return m_document.planes()[index].angleToParentDegrees();
}

bool VpController::planeAngleEditable() const
{
    const int index = m_document.selectedPlane();
    if (index < 0 || index >= m_document.planes().size()
        || m_document.planes()[index].parentPlaneIndex() < 0)
        return false;
    for (const PerspectivePlane &child : m_document.planes()) {
        if (child.parentPlaneIndex() == index && child.hasCustomAngle())
            return false;
    }
    return true;
}

void VpController::setPlaneAngle(qreal angle)
{
    if (!planeAngleEditable() || !qIsFinite(angle))
        return;
    qreal normalized = std::fmod(angle, 360.0);
    if (normalized == 0 && angle > 0)
        normalized = 360;
    else if (normalized < 0)
        normalized += 360;
    if (normalized == planeAngle())
        return;
    cancelInteraction(false);
    const int index = m_document.selectedPlane();
    const PerspectivePlane candidate = rotatePlaneAroundEdge(
        m_document.planes()[index], 0, angle, m_document.background().size());
    m_document.beginEdit();
    if (m_document.setPlane(index, candidate)) {
        m_document.commitEdit(true);
        emit planeAngleChanged();
        emit guidesRepaintRequested();
    } else {
        m_document.cancelEdit();
    }
}

void VpController::setCopiedCanvasSelection(const QImage &image)
{
    m_preferInternalCopy = false;
    m_copiedSelection = FloatingSelection();
    m_copiedSelection.bitmap = image;
    if (!image.isNull() && qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        // Native object copies also expose a flattened bitmap. Preserve the canvas
        // pixel snapshot (including text alpha) instead of that compatibility image.
        m_preferInternalCopy = mime && mime->hasFormat(
            QStringLiteral("application/x-qt-windows-mime;value=\"HIMIRAGE_CUSTOM_FORMAT1\""));
    }
}

void VpController::copySelection()
{
    if (m_document.editActive() || m_marqueeTool.active())
        return;
    finishActiveStrokes();
    FloatingSelection selection;
    if (m_document.selectedFloatingSelection() >= 0)
        selection = m_document.floatingSelection(m_document.selectedFloatingSelection());
    else
        selection = m_marqueeTool.copy(selectionSampleImage());
    if (selection.bitmap.isNull()) {
        postStatus(tr("请先创建选区或选中浮动选区，再复制。"), true);
        return;
    }
    m_copiedSelection = selection;
    m_preferInternalCopy = true;
    postStatus(tr("已复制选区内容，按 Ctrl+V 粘贴为浮动选区。"));
}

void VpController::pasteSelection()
{
    if (!m_document.hasLoadedImage()) return;
    FloatingSelection selection = m_copiedSelection;
    if (!m_preferInternalCopy && qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        const QClipboard *clipboard = QGuiApplication::clipboard();
        const QMimeData *mime = clipboard->mimeData();
        if (mime && !mime->formats().isEmpty()) {
            selection = FloatingSelection();
            if (mime->hasImage())
                selection.bitmap = clipboard->image().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
    }
    if (selection.bitmap.isNull()) {
        postStatus(tr("请先复制选区内容。"), true);
        return;
    }
    cancelInteraction();
    if (m_document.addFloatingSelection(selection) < 0)
        return;
    setTool(VpTools::Marquee);
    emit focusRequested();
    postStatus(tr("已粘贴为浮动选区，可拖动或使用变换工具调整。"));
    emit repaintRequested();
}

void VpController::setBrushHealMode(int mode)
{
    if (mode < HealingBlend::Off || mode > HealingBlend::On || mode == brushHealMode()) return;
    endBrush();
    m_brushTool.setHealMode(mode);
    emit brushChanged();
    emit previewRepaintRequested();
}

void VpController::resetMarqueeMoveMode()
{
    m_marqueeMoveMode = 0;
    m_marqueeSourceLocked = false;
    m_sourceModeFill = false;
    m_sourceFillBase = FloatingSelection();
    m_sourceFillTarget = QImage();
    m_sourceFillPaintBefore = QImage();
    m_sourceFillRect = QRect();
    m_sourceFillPaintKey = 0;
    emit marqueeMoveModeChanged();
}

bool VpController::reapplySourceFill()
{
    if (m_tool != VpTools::Marquee || m_sourceFillBase.bitmap.isNull()
        || m_sourceFillRect.isEmpty() || m_document.selectedFloatingSelection() >= 0
        || m_document.editActive() || m_marqueeTool.outline().isEmpty()
        || m_sourceFillPaintKey != m_document.paintLayer().cacheKey()) return false;
    FloatingSelection selection = m_sourceFillBase;
    selection.healMode = marqueeHealMode();
    selection.opacity = marqueeOpacity();
    selection.setFeather(marqueeFeather());
    selection = healedSelection(selection, m_sourceFillTarget);
    m_document.beginEdit();
    m_document.beginPaintTransaction();
    QPainter painter(&m_document.paintLayer());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    // Replace the previous application, retaining all earlier completed fills.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(m_sourceFillRect.topLeft(), m_sourceFillPaintBefore);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    SceneRenderer::renderFloatingSelection(painter, selection);
    painter.end();
    m_document.addPaintDirty(m_sourceFillRect);
    m_document.commitEdit(true);
    m_sourceFillPaintKey = m_document.paintLayer().cacheKey();
    emit repaintRequested();
    return true;
}

bool VpController::marqueeMoveModeEnabled() const
{
    return m_tool == VpTools::Marquee && !m_marqueeSourceLocked && !m_marqueeTool.active()
        && m_document.selectedFloatingSelection() < 0 && !m_marqueeTool.outline().isEmpty();
}

void VpController::setMarqueeMoveMode(int mode)
{
    if (!marqueeMoveModeEnabled() || mode < 0 || mode > 1 || mode == m_marqueeMoveMode) return;
    m_marqueeMoveMode = mode;
    emit marqueeMoveModeChanged();
}

void VpController::setMarqueeFeather(int value)
{
    value = qBound(0, value, 50);
    if (value == marqueeFeather()) return;
    m_marqueeTool.setFeather(value);
    const int index = m_document.selectedFloatingSelection();
    if (m_tool == VpTools::Marquee && index >= 0 && !m_document.editActive()) {
        FloatingSelection selection = m_document.floatingSelection(index);
        selection.setFeather(value);
        selection = healedSelection(selection, selectionSampleImage(index));
        m_document.beginEdit();
        m_document.commitEdit(m_document.setFloatingSelection(index, selection));
        emit repaintRequested();
    }
    reapplySourceFill();
    emit marqueeChanged();
}

void VpController::syncMarqueeOptions()
{
    const int index = m_document.selectedFloatingSelection();
    if (m_tool != VpTools::Marquee || index < 0) return;
    const FloatingSelection &selection = m_document.floatingSelection(index);
    if (selection.opacity == marqueeOpacity() && selection.healMode == marqueeHealMode()
        && selection.feather == marqueeFeather()) return;
    m_marqueeTool.setFeather(selection.feather);
    m_marqueeTool.setOpacity(selection.opacity);
    m_marqueeTool.setHealMode(selection.healMode);
    emit marqueeChanged();
}

void VpController::setMarqueeOpacity(int value)
{
    value = qBound(0, value, 100);
    if (value == marqueeOpacity()) return;
    m_marqueeTool.setOpacity(value);
    const int index = m_document.selectedFloatingSelection();
    if (m_tool == VpTools::Marquee && index >= 0 && !m_document.editActive()) {
        FloatingSelection selection = m_document.floatingSelection(index);
        selection.opacity = value;
        m_document.beginEdit();
        m_document.commitEdit(m_document.setFloatingSelection(index, selection));
        emit repaintRequested();
    }
    reapplySourceFill();
    emit marqueeChanged();
}

void VpController::setMarqueeHealMode(int mode)
{
    if (mode < HealingBlend::Off || mode > HealingBlend::On || mode == marqueeHealMode()) return;
    m_marqueeTool.setHealMode(mode);
    const int index = m_document.selectedFloatingSelection();
    if (m_tool == VpTools::Marquee && index >= 0 && !m_document.editActive()) {
        FloatingSelection selection = m_document.floatingSelection(index);
        selection.healMode = mode;
        const QImage target = selectionSampleImage(index);
        m_document.beginEdit();
        m_document.commitEdit(m_document.setFloatingSelection(index, healedSelection(selection, target)));
        emit repaintRequested();
    }
    reapplySourceFill();
    emit marqueeChanged();
}

void VpController::setCloneHealMode(int mode)
{
    if (mode < HealingBlend::Off || mode > HealingBlend::On || mode == cloneHealMode()) return;
    endClone();
    m_cloneTool.setHealMode(mode);
    emit cloneChanged();
    emit previewRepaintRequested();
}

bool VpController::beginBrush(const QPointF &point)
{
    if (!isToolEnabled(VpTools::Brush))
        return false;
    m_document.beginPaintTransaction(true);
    m_brushTool.setSelectionClip(selectionOutline());
    const QRect dirty = m_brushTool.begin(m_document.paintLayer(), m_document.planes(),
                                          m_document.background().size(), point,
                                          [this](const QRect &rect) { m_document.preparePaintDirty(rect); },
                                          brushHealMode() == HealingBlend::Off ? QImage() : selectionSampleImage());
    if (!m_brushTool.drawing())
        return false;
    m_strokePlaneIndex = topmostPlaneIndexAt(m_document.planes(), point);
    emit guidesRepaintRequested();
    m_document.addPaintDirty(dirty);
    if (!dirty.isEmpty()) emit paintRepaintRequested(dirty);
    return true;
}

void VpController::moveBrush(const QPointF &point)
{
    if (m_brushTool.drawing()) {
        const QRect dirty = m_brushTool.move(m_document.paintLayer(), point,
                                           [this](const QRect &rect) { m_document.preparePaintDirty(rect); });
        m_document.addPaintDirty(dirty);
        if (!dirty.isEmpty()) emit paintRepaintRequested(dirty);
    }
}

void VpController::endBrush()
{
    if (!m_brushTool.drawing())
        return;
    const QRect dirty = m_brushTool.end(&m_document.paintLayer());
    m_strokePlaneIndex = -1;
    emit guidesRepaintRequested();
    if (!dirty.isEmpty()) {
        m_document.addPaintDirty(dirty);
        emit paintRepaintRequested(dirty);
    }
    m_document.commitHistory();
}

void VpController::renderBrushPreview(QPainter &painter, const QPointF &point) const
{
    painter.save();
    const QPainterPath clip = selectionOutline();
    if (!clip.isEmpty()) painter.setClipPath(clip, Qt::IntersectClip);
    m_brushTool.renderPreview(painter, m_document.planes(), m_document.background().size(), point);
    painter.restore();
}

QRectF VpController::brushPreviewBounds(const QPointF &point) const
{
    const QRectF bounds = m_brushTool.previewBounds(m_document.planes(), m_document.background().size(), point);
    const QPainterPath clip = selectionOutline();
    return clip.isEmpty() ? bounds : bounds.intersected(clip.boundingRect());
}

const QImage &VpController::cloneSource()
{
    if (m_cloneTool.drawing())
        return m_cloneSource;
    // A stroke keeps its own COW snapshot even when sampled content changes.
    m_cloneSource = selectionSampleImage();
    return m_cloneSource;
}

bool VpController::pickCloneSource(const QPointF &point)
{
    return isToolEnabled(VpTools::CloneStamp)
        && m_cloneTool.pickSource(m_document.planes(), m_document.background().size(), point);
}

bool VpController::beginClone(const QPointF &point)
{
    if (!isToolEnabled(VpTools::CloneStamp) || !m_cloneTool.hasSource())
        return false;
    m_document.beginPaintTransaction(true);
    m_cloneTool.setSelectionClip(selectionOutline());
    const QRect dirty = m_cloneTool.begin(m_document.paintLayer(), cloneSource(),
                                          m_document.planes(), m_document.background().size(), point,
                                          [this](const QRect &rect) { m_document.preparePaintDirty(rect); });
    if (!m_cloneTool.drawing())
        return false;
    m_strokePlaneIndex = topmostPlaneIndexAt(m_document.planes(), point);
    emit guidesRepaintRequested();
    m_document.addPaintDirty(dirty);
    if (!dirty.isEmpty()) emit paintRepaintRequested(dirty);
    return true;
}

void VpController::moveClone(const QPointF &point)
{
    if (m_cloneTool.drawing()) {
        const QRect dirty = m_cloneTool.move(m_document.paintLayer(), point,
                                           [this](const QRect &rect) { m_document.preparePaintDirty(rect); });
        m_document.addPaintDirty(dirty);
        if (!dirty.isEmpty()) emit paintRepaintRequested(dirty);
    }
}

void VpController::endClone()
{
    if (!m_cloneTool.drawing())
        return;
    const QRect dirty = m_cloneTool.end(&m_document.paintLayer());
    m_strokePlaneIndex = -1;
    emit guidesRepaintRequested();
    if (!dirty.isEmpty()) {
        m_document.addPaintDirty(dirty);
        emit paintRepaintRequested(dirty);
    }
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
    painter.save();
    const QPainterPath clip = selectionOutline();
    if (!clip.isEmpty()) painter.setClipPath(clip, Qt::IntersectClip);
    m_cloneTool.renderPreview(painter, cloneSource(), m_document.planes(),
                              m_document.background().size(), point);
    painter.restore();
}

void VpController::finishActiveStrokes()
{
    endBrush();
    endClone();
}

QRectF VpController::clonePreviewBounds(const QPointF &point) const
{
    const QRectF bounds = m_cloneTool.previewBounds(m_document.planes(), m_document.background().size(), point);
    const QPainterPath clip = selectionOutline();
    return clip.isEmpty() ? bounds : bounds.intersected(clip.boundingRect());
}

QImage VpController::resultImage() const
{
    QImage result(m_document.background().size(), QImage::Format_ARGB32_Premultiplied);
    if (result.isNull())
        return result;
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.drawImage(QPointF(), m_document.paintLayer());
    SceneRenderer renderer(m_document);
    for (const FloatingSelection &image : m_document.floatingSelections())
        renderer.renderFloatingSelection(painter, image);
    painter.end();
    return result;
}

bool VpController::applyChanges()
{
    finishActiveStrokes();
    const QImage result = resultImage();
    return !result.isNull() && m_applyHandler && m_applyHandler(result);
}

void VpController::pointerPress(const QPointF &point, qreal viewScale, Qt::KeyboardModifiers modifiers)
{
    if (tool() == VpTools::Eyedropper) {
        pickBrushColor(point);
        return;
    }
    if (tool() == VpTools::EditPlane && (modifiers & Qt::ControlModifier)
        && !m_startingCtrlPlaneDrag) {
        // Defer Ctrl clicks until release so edge drags retain their existing action.
        m_ctrlPlaneCyclePending = true;
        m_ctrlPlanePressPoint = point;
        m_ctrlPlaneViewScale = qMax(viewScale, 1e-6);
        m_ctrlPlanePressModifiers = modifiers;
        return;
    }
    if (beginFloatingSelectionInteraction(point, viewScale)) // 浮动选区浮在最上层，先于任何工具处理
        return;
    switch (tool()) {
    case VpTools::EditPlane: {
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
            emit guidesRepaintRequested();
            return;
        }

        // 选中平面
        m_document.setSelectedPlane(planeIndex);
        notifyPlaneAngleChanged();
        // 与相邻平面共边的平面不能整体平移，否则共用边会被撕开
        if (handle < 0 && m_document.isPlaneLinked(planeIndex)) {
            postStatus(tr("该平面已与相邻平面共边，不能整体移动。"), true);
            emit guidesRepaintRequested();
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
        if (rotate && !planeAngleEditable())
            return;
        m_editPlaneIndex = planeIndex;
        m_extrudePreviewReady = false;
        m_editTool.begin(plane, point, handle, edge, extrude, m_document.background().size(),
                         rotate, rotate ? 0 : -1);
        m_document.beginEdit();
        if (extrude)
            setInteractionHint(tr("拖动以拉出垂直平面，松开完成。"));
        else if (rotate)
            setInteractionHint(tr("拖动以调整与父平面的夹角，松开完成。"));
        emit guidesRepaintRequested();
        return;
    }
    case VpTools::CreatePlane:
        m_createTool.addPoint(point);
        if (m_createTool.finished())
            finishPlaneCreation();
        else
            reportCreateProgress();
        emit guidesRepaintRequested();
        return;
    case VpTools::Brush: {
        beginBrush(point);
        return;
    }
    case VpTools::CloneStamp: {
        // Alt+单击只取源点，不落笔
        if (modifiers & Qt::AltModifier) {
            const bool picked = pickCloneSource(point);
            postStatus(picked ? tr("已设置仿制源，按住 Alt 可重新取样")
                                      : tr("此处无法作为仿制源。"), !picked);
            emit repaintRequested();
            return;
        }
        if (!hasCloneSource()) {
            postStatus(tr("请先按住 Alt 单击，设置仿制源。"), true);
            return;
        }
        beginClone(point);
        return;
    }
    case VpTools::Marquee: {
        QPointF surface;
        const bool insideSelection = m_marqueeTool.contains(point)
                                     && m_marqueeTool.mapToSurface(point, &surface);
        // Alt 拖动：复制内容为浮动选区，并立即接续图像移动。
        if (insideSelection && (modifiers & Qt::AltModifier)) {
            const int index = appendFloatingSelection(m_marqueeTool.copy(selectionSampleImage(), point));
            if (index >= 0) {
                m_draggedFloatingSelectionIndex = index;
                m_document.beginEdit();
                m_floatingSelectionTransform.beginMove(
                    m_document.floatingSelection(index), surface - m_document.floatingSelection(index).placementOrigin);
                m_marqueeTool.clear();
                resetMarqueeMoveMode();
                postStatus(tr("已复制选区内容为浮动选区。"));
            }
            emit repaintRequested();
            return;
        }
        if (insideSelection) {
            if (m_marqueeMoveMode == 1 || (modifiers & Qt::ControlModifier)) {
                const QImage source = selectionSampleImage();
                if (m_marqueeTool.beginFill(point, source, m_document.paintLayer())) {
                    m_sourceModeFill = m_marqueeMoveMode == 1;
                    if (m_sourceModeFill) m_marqueeSourceLocked = true;
                    emit marqueeMoveModeChanged();
                    m_document.beginEdit();
                    updateSelection(point, modifiers);
                }
            } else {
                m_marqueeTool.beginMove(point);
            }
        } else {
            resetMarqueeMoveMode();
            m_marqueeTool.beginCreate(m_document.planes(), point);
            emit marqueeMoveModeChanged();
        }
        if (m_marqueeTool.action() == MarqueeTool::Action::Fill) emit previewRepaintRequested();
        else emit guidesRepaintRequested();
        return;
    }
    default:
        return; // 其余工具尚未实现
    }
}
void VpController::pointerMove(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    if (m_ctrlPlaneCyclePending) {
        if (QLineF(m_ctrlPlanePressPoint, point).length() * m_ctrlPlaneViewScale < 4.0)
            return;
        m_ctrlPlaneCyclePending = false;
        m_startingCtrlPlaneDrag = true;
        pointerPress(m_ctrlPlanePressPoint, m_ctrlPlaneViewScale, m_ctrlPlanePressModifiers);
        m_startingCtrlPlaneDrag = false;
    }
    if (tool() == VpTools::Eyedropper) {
        pickBrushColor(point);
        return;
    }
    if (m_marqueeTool.active()) {
        updateSelection(point, modifiers);
        return;
    }
    if (m_draggedFloatingSelectionIndex >= 0) {
        updateFloatingSelectionInteraction(point, modifiers);
        return;
    }
    switch (tool()) {
    case VpTools::EditPlane: {
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
            m_document.setPlane(m_editPlaneIndex, candidate, m_editTool.resizing());
            if (m_editTool.rotating())
                notifyPlaneAngleChanged(); // 角度滑杆随拖动实时跟走
        }
        emit guidesRepaintRequested();
        return;
    }
    case VpTools::Brush:
        if (brushDrawing()) {
            moveBrush(point);
        }
        return;
    case VpTools::CloneStamp:
        if (cloneDrawing()) {
            moveClone(point);
        }
        return;
    default:
        return;
    }
}
void VpController::pointerRelease(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    if (m_ctrlPlaneCyclePending) {
        pointerMove(point, modifiers);
        if (m_ctrlPlaneCyclePending) {
            m_ctrlPlaneCyclePending = false;
            QVector<int> candidates;
            const qreal tolerance = qMax(8.0 / m_ctrlPlaneViewScale, 4.0);
            for (int i = m_document.planes().size() - 1; i >= 0; --i) {
                const auto &quad = m_document.planes()[i].quad();
                if (quad.containsCanvasPoint(m_ctrlPlanePressPoint)
                    || quad.controlPointIndexAt(m_ctrlPlanePressPoint, tolerance) >= 0)
                    candidates.append(i);
            }
            const int current = candidates.indexOf(m_document.selectedPlane());
            m_document.setSelectedPlane(candidates.isEmpty() ? -1
                : candidates[(current + 1) % candidates.size()]);
            notifyPlaneAngleChanged();
            emit guidesRepaintRequested();
            return;
        }
    }
    if (tool() == VpTools::Eyedropper) {
        pickBrushColor(point);
        return;
    }
    // 缩放/旋转的最终几何取松开时的位置，避免漏掉最后一次移动；图像拖动
    // 可以在任何工具下进行，所以先于工具分派收尾。
    if (m_draggedFloatingSelectionIndex >= 0)
        updateFloatingSelectionInteraction(point, modifiers);
    if (endFloatingSelectionInteraction())
        return;
    pointerMove(point, modifiers);
    switch (tool()) {
    case VpTools::EditPlane: {
        if (m_editPlaneIndex < 0)
            return;
        setInteractionHint(QString());
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
        emit guidesRepaintRequested();
        return;
    }
    case VpTools::Brush:
        if (brushDrawing()) {
            moveBrush(point);
            endBrush();
            emit repaintRequested();
        }
        return;
    case VpTools::CloneStamp:
        if (cloneDrawing()) {
            endClone();
            emit repaintRequested();
        }
        return;
    case VpTools::Marquee: {
        if (!m_marqueeTool.active())
            return;
        updateSelection(point, modifiers);
        if (m_marqueeTool.action() == MarqueeTool::Action::Fill && m_sourceModeFill) {
            m_sourceFillBase = m_marqueeTool.clone();
            m_sourceFillTarget = selectionSampleImage();
            const FloatingSelection filled = healedSelection(m_sourceFillBase, m_sourceFillTarget);
            m_sourceFillRect = FloatingSelectionProjection::forSelection(filled)->canvasPaintOutline()
                .boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2)
                .intersected(m_document.paintLayer().rect());
            m_sourceFillPaintBefore = m_document.paintLayer().copy(m_sourceFillRect);
            if (!filled.bitmap.isNull()) {
                m_document.beginPaintTransaction();
                QPainter painter(&m_document.paintLayer());
                painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
                SceneRenderer::renderFloatingSelection(painter, filled);
                painter.end();
                m_document.addPaintDirty(FloatingSelectionProjection::forSelection(filled)->canvasPaintOutline()
                    .boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2)
                    .intersected(m_document.paintLayer().rect()));
            }
            m_document.commitEdit(!filled.bitmap.isNull());
            m_sourceFillPaintKey = m_document.paintLayer().cacheKey();
            m_selectionPreview = FloatingSelection();
            m_marqueeTool.end();
            emit marqueeMoveModeChanged();
            emit previewRepaintRequested();
            emit repaintRequested();
            emit guidesRepaintRequested();
            return;
        }
        if (m_marqueeTool.action() == MarqueeTool::Action::Fill) {
            // 清理独立预览，再按原分辨率落成浮动选区。
            m_document.commitEdit(false);
            m_selectionPreview = FloatingSelection();
            emit previewRepaintRequested();
            const int index = appendFloatingSelection(healedSelection(m_marqueeTool.clone(), selectionSampleImage()));
            if (index >= 0) {
                m_marqueeTool.clear();
                resetMarqueeMoveMode();
                postStatus(tr("已把拖动结果生成为浮动选区，可直接拖动移动。"));
                emit repaintRequested();
                return;
            }
        }
        // 结束交互并清理取样快照；有效选区继续保留。
        const bool filling = m_marqueeTool.action() == MarqueeTool::Action::Fill;
        m_marqueeTool.end();
        emit marqueeMoveModeChanged();
        if (filling) emit repaintRequested();
        else emit guidesRepaintRequested();
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
    setInteractionHint(QString());

    if (!plane.quad().isValid()) {
        postStatus(tr("无法创建：四个点必须依次组成非交叉的凸四边形，请重新设置。"), true);
        return;
    }
    m_document.beginEdit();
    const int index = m_document.appendPlane(plane);
    m_document.setSelectedPlane(index);
    m_document.commitEdit(true);

    setTool(VpTools::EditPlane);
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

void VpController::cancelInteraction(bool clearMarquee)
{
    setInteractionHint(QString());
    emit statusMessageCleared();
    m_ctrlPlaneCyclePending = false;
    m_selectionPreview = FloatingSelection();
    emit previewRepaintRequested();
    m_createTool.reset();
    m_extrudePreviewReady = false; // 拖出垂直平面的预览随交互一起作废
    // 进行中的笔触已经烘焙进绘画层，切换工具时提交。
    finishActiveStrokes();
    // 平面拖动、图像拖动与 Ctrl 克隆都只改了结构或绘画预览，可以直接丢弃
    if (m_editPlaneIndex >= 0 || m_draggedFloatingSelectionIndex >= 0
        || m_marqueeTool.active()) {
        m_editPlaneIndex = -1;
        m_floatingSelectionTransform.reset();
        m_draggedFloatingSelectionIndex = -1;

        m_document.cancelEdit();
    }
    if (clearMarquee || m_marqueeTool.active()) {
        m_marqueeTool.clear();
        resetMarqueeMoveMode();
    }
    emit guidesRepaintRequested();
}

void VpController::deleteSelectedPlane()
{
    // 拖动编辑进行中：先丢弃这次拖动，否则后续鼠标事件会写回已经移位的平面下标
    if (m_editPlaneIndex >= 0 || m_ctrlPlaneCyclePending)
        cancelInteraction();
    const int index = m_document.selectedPlane();
    if (index < 0)
        return;
    m_document.removePlane(index);
    notifyPlaneAngleChanged(); // 删除会改变选中项，也可能解开上一级父平面的夹角锁定
    postStatus(tr("已删除选中的平面。"));
    emit guidesRepaintRequested();
}

void VpController::setInteractionHint(const QString &text)
{
    if (m_interactionHint == text)
        return;
    m_interactionHint = text;
    if (!text.isEmpty())
        emit statusMessageCleared();
    emit interactionHintChanged();
}

void VpController::reportCreateProgress()
{
    const int count = m_createTool.points().size();
    if (count == 0)
        setInteractionHint(tr("已回退全部角点，请重新点击"));
    else
        setInteractionHint(tr("已设置 %1/%2 个角点").arg(count).arg(PlaneCreateTool::CornerCount));
}

int VpController::paintTargetPlane(const QPointF &point) const
{
    if ((tool() != VpTools::Brush && tool() != VpTools::CloneStamp && tool() != VpTools::Marquee)
        || m_draggedFloatingSelectionIndex >= 0)
        return -1;
    const int index = m_brushTool.drawing() || m_cloneTool.drawing()
        ? m_strokePlaneIndex : topmostPlaneIndexAt(m_document.planes(), point);
    return index >= 0 && index < m_document.planes().size()
        && m_document.planes()[index].quad().isProjectable() ? index : -1;
}

bool VpController::cursorPreviewVisible() const
{
    if (tool() == VpTools::Brush)
        return true;
    if (tool() == VpTools::CloneStamp)
        return hasCloneSource();
    return tool() == VpTools::CreatePlane && m_createTool.creating();
}

QString VpController::interactionCursorResource(const QPointF &point, qreal viewScale) const
{
    if (!m_document.hasLoadedImage() || tool() == VpTools::Eyedropper)
        return {};
    const int selected = m_document.selectedFloatingSelection();
    if (tool() == VpTools::Transform && selected >= 0) {
        const auto &image = m_document.floatingSelection(selected);
        const int handle = FloatingSelectionTransformTool::handleAt(image, point, viewScale);
        if (handle >= 0) {
            const auto projection = FloatingSelectionProjection::forSelection(image);
            const auto &handles = projection->transformHandles();
            static const int opposite[] = {2, 3, 0, 1, 6, 7, 4, 5};
            QPointF direction = handles[handle] - handles[opposite[handle]];
            if (handle < 4) {
                // Equal edge weights keep corner cursors diagonal regardless of aspect ratio,
                // while following the projected edges when rotated or in perspective.
                const QPointF a = handles[handle] - handles[(handle + 1) % 4];
                const QPointF b = handles[handle] - handles[(handle + 3) % 4];
                const qreal lengthA = std::hypot(a.x(), a.y());
                const qreal lengthB = std::hypot(b.x(), b.y());
                if (lengthA > 1e-6 && lengthB > 1e-6)
                    direction = a / lengthA + b / lengthB;
            }
            const int axis = (qRound(std::atan2(direction.y(), direction.x()) * 4 / 3.141592653589793) % 4 + 4) % 4;
            static const char *resources[] = {"cut_horizontal.svg", "cut_oblique_back.svg",
                                              "cut_vertical.svg", "cut_oblique.svg"};
            return QString::fromLatin1(resources[axis]);
        }
        if (FloatingSelectionTransformTool::rotationCornerAt(image, point, viewScale) >= 0)
            return QStringLiteral("image_cursor_rotate.svg");
    }
    int hit = -1;
    QPointF offset;
    if (m_document.background().rect().contains(point.toPoint())
        && floatingSelectionAt(point, &hit, &offset)
        && (tool() != VpTools::Marquee || hit == selected))
        return QStringLiteral("cursor_selection_move.svg");
    if (tool() == VpTools::EditPlane && selected < 0) {
        const qreal tolerance = qMax(8.0 / qMax(viewScale, 1e-6), 4.0);
        for (int i = m_document.planes().size() - 1; i >= 0; --i) {
            const auto &plane = m_document.planes()[i];
            const int handle = plane.quad().controlPointIndexAt(point, tolerance);
            if (handle < 0 || !plane.controlPointEditable(handle)) continue;
            if (handle < 4) return QStringLiteral("image_choice.svg");
            const auto &corners = plane.quad().canvasCorners();
            const int edge = handle - 4;
            const QPointF direction = (corners[edge] + corners[(edge + 1) % 4])
                - (corners[(edge + 2) % 4] + corners[(edge + 3) % 4]);
            return qAbs(direction.x()) >= qAbs(direction.y())
                ? QStringLiteral("cut_horizontal.svg") : QStringLiteral("cut_vertical.svg");
        }
    }
    QPointF surface;
    if (tool() == VpTools::Marquee && selected < 0 && m_marqueeTool.contains(point)
        && m_marqueeTool.mapToSurface(point, &surface))
        return QStringLiteral("cursor_selection_move.svg");
    if (tool() == VpTools::Marquee || tool() == VpTools::Brush || tool() == VpTools::CloneStamp)
        return QStringLiteral("image_precinct.svg");
    if (tool() == VpTools::EditPlane || tool() == VpTools::CreatePlane)
        return QStringLiteral("image_choice.svg");
    return {};
}

qreal VpController::interactionCursorRotation(const QPointF &point, qreal viewScale) const
{
    const int selected = m_document.selectedFloatingSelection();
    if (tool() != VpTools::Transform || selected < 0)
        return 0;
    const auto &image = m_document.floatingSelection(selected);
    const int corner = FloatingSelectionTransformTool::rotationCornerAt(image, point, viewScale);
    if (corner < 0)
        return 0;
    const auto projection = FloatingSelectionProjection::forSelection(image);
    const auto &handles = projection->transformHandles();
    const QPointF a = handles[corner] - handles[(corner + 1) % 4];
    const QPointF b = handles[corner] - handles[(corner + 3) % 4];
    const qreal lengthA = std::hypot(a.x(), a.y());
    const qreal lengthB = std::hypot(b.x(), b.y());
    if (lengthA < 1e-6 || lengthB < 1e-6)
        return 0;
    const QPointF direction = a / lengthA + b / lengthB;
    // The existing artwork bends around the top-right corner (outward angle -45 degrees).
    return qRound(std::atan2(direction.y(), direction.x()) * 180 / 3.141592653589793 + 45);
}

bool VpController::beginFloatingSelectionInteraction(const QPointF &point, qreal viewScale)
{
    if (tool() == VpTools::Transform && m_document.selectedFloatingSelection() >= 0) {
        FloatingSelection image = m_document.floatingSelection(m_document.selectedFloatingSelection());
        if (!image.unhealedBitmap.isNull()) image.bitmap = image.unhealedBitmap;
        const int handle = FloatingSelectionTransformTool::handleAt(image, point, viewScale);
        const int corner = handle < 0
            ? FloatingSelectionTransformTool::rotationCornerAt(image, point, viewScale) : -1;
        const auto mode = corner >= 0 ? FloatingSelectionTransformTool::Mode::Rotate
                                      : FloatingSelectionTransformTool::Mode::Scale;
        if ((handle >= 0 || corner >= 0)
            && m_floatingSelectionTransform.beginTransform(
                image, point, corner >= 0 ? corner : handle, mode)) {
            m_draggedFloatingSelectionIndex = m_document.selectedFloatingSelection();
            m_document.beginEdit();
            m_document.setFloatingSelection(m_draggedFloatingSelectionIndex, image);
            emit repaintRequested();
            return true;
        }
    }

    QPointF grabOffset;
    int grabbed = -1;
    const bool hitSelection = m_document.background().rect().contains(point.toPoint())
                          && floatingSelectionAt(point, &grabbed, &grabOffset);
    if (!hitSelection && m_document.selectedFloatingSelection() >= 0) {
        // 点到别处：把选中的图像烘焙进绘画层，之后不再是可操作对象。
        // 这一下点击只用于确认烘焙，不再触发工具的其它动作，避免误落一笔。
        bakeSelectedFloatingSelection();
        return true;
    }
    if (hitSelection) {
        // 选框工具下只有已选中的图像才拦截点击；点到未选中的图像留给选区建立，
        // 否则贴过图的平面上就再也拖不出新选区。
        if (tool() == VpTools::Marquee && grabbed != m_document.selectedFloatingSelection())
            return false;
        m_draggedFloatingSelectionIndex = grabbed;
        m_document.setSelectedFloatingSelection(grabbed);
        m_document.beginEdit();
        FloatingSelection image = m_document.floatingSelection(grabbed);
        if (!image.unhealedBitmap.isNull()) image.bitmap = image.unhealedBitmap;
        m_document.setFloatingSelection(grabbed, image);
        m_floatingSelectionTransform.beginMove(image, grabOffset);
        emit repaintRequested();
        return true;
    }
    return false;
}

void VpController::updateFloatingSelectionInteraction(const QPointF &point,
                                              Qt::KeyboardModifiers modifiers)
{
    if (m_draggedFloatingSelectionIndex < 0 || m_draggedFloatingSelectionIndex >= m_document.floatingSelections().size()) {
        cancelInteraction();
        return;
    }
    if (m_floatingSelectionTransform.isTransforming()) {
        FloatingSelection image;
        if (m_floatingSelectionTransform.update(
                point, modifiers & Qt::ShiftModifier, modifiers & Qt::AltModifier, &image)) {
            m_document.setFloatingSelection(m_draggedFloatingSelectionIndex, image);
        }
        emit repaintRequested();
        return;
    }
    const FloatingSelection &start = m_floatingSelectionTransform.startSelection();
    if (tool() == VpTools::Transform && start.surfaceAttached) {
        QPointF surface;
        if (start.mapCanvasToPlacement(point, &surface)) {
            m_document.setFloatingSelectionOrigin(
                m_draggedFloatingSelectionIndex,
                surface - m_floatingSelectionTransform.grabOffset());
        }
    } else if (const int plane = topmostPlaneIndexAt(m_document.planes(), point); plane >= 0) {
        attachFloatingSelectionToPlane(m_draggedFloatingSelectionIndex, plane, point);

    } else if (!m_document.floatingSelection(m_draggedFloatingSelectionIndex).surfaceAttached
               || !moveSurfaceAttachedSelection(m_draggedFloatingSelectionIndex, point)) {
        m_document.detachFloatingSelection(
            m_draggedFloatingSelectionIndex, point - m_floatingSelectionTransform.grabOffset());

    }
    emit repaintRequested();
}

bool VpController::endFloatingSelectionInteraction()
{
    if (m_draggedFloatingSelectionIndex < 0)
        return false;
    const int index = m_draggedFloatingSelectionIndex;
    const FloatingSelection selection = m_document.floatingSelection(index);
    if (selection.healMode != HealingBlend::Off)
        m_document.setFloatingSelection(index, healedSelection(selection, selectionSampleImage(index)));
    m_floatingSelectionTransform.reset();
    m_draggedFloatingSelectionIndex = -1;
    m_document.commitEdit(true); // 模型按实际状态比较，空操作不占历史

    emit repaintRequested();
    return true;
}

bool VpController::floatingSelectionAt(const QPointF &point, int *index, QPointF *grabOffset) const
{
    for (int i = m_document.floatingSelections().size() - 1; i >= 0; --i) {
        if (FloatingSelectionProjection::forSelection(m_document.floatingSelection(i))->hitTest(point, grabOffset)) {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

void VpController::attachFloatingSelectionToPlane(int index, int planeIndex, const QPointF &point)
{
    const PerspectivePlane &host = m_document.planes()[planeIndex];
    if (!host.quad().isProjectable()) return;
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
    m_document.attachFloatingSelection(
        index, surfaceQuads, hostQuadIndex,
        surfacePoint - m_floatingSelectionTransform.grabOffset());
}

bool VpController::moveSurfaceAttachedSelection(int index, const QPointF &point)
{
    const FloatingSelection &image = m_document.floatingSelection(index);
    auto moveOnQuad = [this, index, &point](const PerspectiveQuad &quad) {
        QPointF surfacePoint;
        const bool ok = quad.surfaceToCanvasTransform().mapInverse(point, &surfacePoint);
        if (ok)
            m_document.setFloatingSelectionOrigin(
                index, surfacePoint - m_floatingSelectionTransform.grabOffset());
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

void VpController::bakeSelectedFloatingSelection()
{
    if (m_document.selectedFloatingSelection() < 0)
        return;
    const int index = m_document.selectedFloatingSelection();
    m_document.beginEdit();
    m_document.beginPaintTransaction();
    QPainter painter(&m_document.paintLayer());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QRect dirty;
    for (int i = 0; i <= index; ++i) {
        const FloatingSelection &image = m_document.floatingSelection(i);
        SceneRenderer(m_document).renderFloatingSelection(painter, image);
        dirty = dirty.united(FloatingSelectionProjection::forSelection(image)->canvasPaintOutline()
                            .boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2));
    }
    painter.end();
    m_document.addPaintDirty(dirty.intersected(m_document.paintLayer().rect()));
    for (int i = index; i >= 0; --i)
        m_document.removeFloatingSelection(i);
    m_document.commitEdit(true);
    m_floatingSelectionTransform.reset();
    m_draggedFloatingSelectionIndex = -1;

    postStatus(tr("浮动选区已合并到绘画层。"));
    emit repaintRequested();
}

QImage VpController::selectionSampleImage(int excludedSelection) const
{
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << m_document.background().cacheKey() << m_document.paintLayer().cacheKey()
           << qint64(m_document.floatingSelections().size()) << excludedSelection;
    for (const FloatingSelection &image : m_document.floatingSelections())
        stream << image.bitmap.cacheKey() << image.opacity << FloatingSelectionProjection::cacheKey(image);
    if (key == m_sampleKey && !m_sampleImage.isNull()) return m_sampleImage;
    QImage source(m_document.background().size(), QImage::Format_ARGB32_Premultiplied);
    if (source.isNull()) return {};
    source.fill(Qt::transparent);
    QPainter painter(&source);
    if (excludedSelection < 0) {
        SceneRenderer(m_document).renderContent(painter);
    } else {
        painter.drawImage(QPointF(), m_document.background());
        painter.drawImage(QPointF(), m_document.paintLayer());
        for (int i = 0; i < m_document.floatingSelections().size(); ++i)
            if (i != excludedSelection)
                SceneRenderer::renderFloatingSelection(painter, m_document.floatingSelection(i));
    }
    painter.end();
    m_sampleKey = key; m_sampleImage = source; ++m_sampleBuildCount;
    return m_sampleImage;
}

int VpController::appendFloatingSelection(const FloatingSelection &image)
{
    if (image.bitmap.isNull()) {
        postStatus(tr("无法提取选区：内容为空、尺寸超过 8192、输出超过 64 MiB 或内存不足。"), true);
        return -1;
    }
    return m_document.addFloatingSelection(image);
}

FloatingSelection VpController::healedSelection(const FloatingSelection &selection, const QImage &target) const
{
    FloatingSelection result = selection;
    if (!result.unhealedBitmap.isNull()) result.bitmap = result.unhealedBitmap;
    if (result.healMode == HealingBlend::Off || result.bitmap.isNull()) return result;
    if (result.unhealedBitmap.isNull()) result.unhealedBitmap = result.bitmap;
    const QRectF bitmapRect(QPointF(), QSizeF(result.bitmap.size()));
    const QPolygonF bitmapQuad(QVector<QPointF>{bitmapRect.topLeft(), bitmapRect.topRight(),
                                               bitmapRect.bottomRight(), bitmapRect.bottomLeft()});
    const PerspectiveTransform identity(bitmapQuad, bitmapQuad);
    const auto projection = FloatingSelectionProjection::forSelection(result);
    for (const auto &patch : projection->patches()) {
        const QRectF bounds = patch.bitmapClip.boundingRect();
        const qreal radius = qMax(.5, qMin(bounds.width(), bounds.height()) / 2.6);
        const HealingBlend heal(result.healMode, target, patch.bitmapToCanvas, bounds.center(),
                                radius, {}, &identity, {}, &result.unhealedBitmap);
        const QRect area = bounds.toAlignedRect().intersected(result.bitmap.rect());
        for (int y = area.top(); y <= area.bottom(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(result.bitmap.scanLine(y));
            for (int x = area.left(); x <= area.right(); ++x) {
                const QPointF point(x + .5, y + .5);
                if (patch.bitmapClip.contains(point))
                    row[x] = heal.blend(row[x], point);
            }
        }
    }
    return result;
}

void VpController::updateSelection(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    m_marqueeTool.update(point, modifiers, gridSize());
    if (m_marqueeTool.action() == MarqueeTool::Action::Fill) {
        const QRectF rect = m_marqueeTool.rect();
        const QRectF canvas = m_marqueeTool.outline().boundingRect();
        const qreal projectionScale = qMax(canvas.width() / qMax(1.0, rect.width()),
                                            canvas.height() / qMax(1.0, rect.height()));
        const qreal scale = qMin(1.0, qMin(2048.0 / qMax(1.0, qMax(rect.width(), rect.height())),
                                              2 * m_selectionPreviewDeviceScale * projectionScale));
        m_selectionPreview = m_marqueeTool.clone(scale);
        emit previewRepaintRequested();
    } else emit guidesRepaintRequested();
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

void VpController::clearSelection()
{
    finishActiveStrokes();
    // Ctrl+D clears the marquee only; floating content and plane editing stay intact.
    if (m_marqueeTool.active())
        cancelInteraction();
    else
        m_marqueeTool.clear();
    resetMarqueeMoveMode();
    m_selectionPreview = FloatingSelection();
    emit previewRepaintRequested();
    emit guidesRepaintRequested();
    emit repaintRequested();
}

void VpController::deleteSelection()
{
    if (m_createTool.creating()) {
        m_createTool.removeLastPoint();
        reportCreateProgress();
    } else {
        cancelInteraction();
        if (m_document.selectedFloatingSelection() >= 0) {
            m_document.removeFloatingSelection(m_document.selectedFloatingSelection());
            postStatus(tr("已删除选中的图像。"));
        } else {
            deleteSelectedPlane();
        }
    }
    emit repaintRequested();
}

// Standalone demo entry point; the host application supplies setImage directly.
bool VpController::openImage(const QUrl &url)
{
    if (!url.isLocalFile()) {
        postStatus(tr("请选择本地图片文件。"), true);
        return false;
    }
    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() * 4 > 128 * 1024 * 1024) {
        postStatus(tr("图片打开失败：文件不可读、格式不支持或超过 128 MiB 解码限制。"), true);
        return false;
    }
    QImageReader::setAllocationLimit(128);
    const QImage image = reader.read();
    if (image.isNull()) {
        postStatus(tr("图片打开失败。"), true);
        return false;
    }
    cancelInteraction();
    if (!m_document.setImage(image)) {
        postStatus(tr("图片打开失败。"), true);
        return false;
    }
    m_sourceImageUrl = url;
    setTool(VpTools::CreatePlane);
    postStatus(tr("图片已打开，请先创建平面。"));
    emit focusRequested();
    return true;
}

QUrl VpController::suggestedSaveUrl() const
{
    if (!m_savedImageUrl.isEmpty())
        return m_savedImageUrl;
    if (m_sourceImageUrl.isLocalFile()) {
        const QFileInfo source(m_sourceImageUrl.toLocalFile());
        return QUrl::fromLocalFile(source.dir().filePath(source.completeBaseName() + "_edited.png"));
    }
    QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (folder.isEmpty()) folder = QDir::homePath();
    return QUrl::fromLocalFile(QDir(folder).filePath("vanishing-point.png"));
}

bool VpController::saveImage(const QUrl &url)
{
    if (!canSave()) {
        postStatus(tr("请先打开图片再保存。"), true);
        return false;
    }
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) {
        postStatus(tr("请选择本地图片保存位置。"), true);
        return false;
    }
    QString fileName = url.toLocalFile();
    QByteArray format = QFileInfo(fileName).suffix().toLower().toLatin1();
    if (format.isEmpty()) {
        fileName += ".png";
        format = "png";
    }
    if (format != "png" && format != "jpg" && format != "jpeg" && format != "bmp") {
        postStatus(tr("保存失败：请选择 PNG、JPEG 或 BMP 格式。"), true);
        return false;
    }
    finishActiveStrokes();
    const bool opaque = format != "png";
    QImage image(m_document.background().size(), opaque ? QImage::Format_RGB32
                                                       : QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        postStatus(tr("保存失败：无法分配图像内存。"), true);
        return false;
    }
    image.fill(opaque ? Qt::white : Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        SceneRenderer(m_document).renderContent(painter);
    }
    // Encode to a temporary file so a failed write cannot damage an existing image.
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        postStatus(tr("保存失败：%1").arg(file.errorString()), true);
        return false;
    }
    QImageWriter writer(&file, format);
    if (format == "jpg" || format == "jpeg") writer.setQuality(95);
    if (!writer.write(image)) {
        postStatus(tr("保存失败：%1").arg(writer.errorString()), true);
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        postStatus(tr("保存失败：%1").arg(file.errorString()), true);
        return false;
    }
    const QUrl savedUrl = QUrl::fromLocalFile(fileName);
    if (m_savedImageUrl != savedUrl) {
        m_savedImageUrl = savedUrl;
        emit savedImageUrlChanged();
    }
    postStatus(tr("图片已保存：%1").arg(QDir::toNativeSeparators(fileName)));
    return true;
}
