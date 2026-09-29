#include "vpcontroller.h"

#include "core/floatingimageprojection.h"
#include "core/scenerenderer.h"

#include <QClipboard>
#include <QDataStream>
#include <QGuiApplication>
#include <QIODevice>
#include <QPainter>

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
    emit aboutToChangeTool();
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
    emit aboutToExecuteCommand();
    finishActiveStrokes();
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
    emit aboutToExecuteCommand();
    finishActiveStrokes();
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
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << m_document.background().cacheKey() << m_document.paintLayer().cacheKey()
           << qint64(m_document.floatingImages().size());
    for (const FloatingImage &image : m_document.floatingImages())
        stream << image.bitmap.cacheKey() << FloatingImageProjection::cacheKey(image);
    if (m_cloneTool.drawing() || (key == m_cloneSourceKey && !m_cloneSource.isNull()))
        return m_cloneSource;
    m_cloneSourceKey = key;
    m_cloneSource = QImage(m_document.background().size(), QImage::Format_ARGB32_Premultiplied);
    m_cloneSource.fill(Qt::transparent);
    QPainter painter(&m_cloneSource);
    SceneRenderer(m_document).render(painter, 1.0, /*showGuides*/ false);
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
    m_cloneTool.renderPreview(painter, cloneSource(), m_document.planes(),
                              m_document.background().size(), point);
}

void VpController::finishActiveStrokes()
{
    endBrush();
    endClone();
}
