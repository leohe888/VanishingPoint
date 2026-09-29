#include "vpdocument.h"

#include <QPainter>
#include <QImageReader>
#include <QSet>

namespace {
// 历史条目可能包含多张大纹理的脏矩形，因此限制历史数量以防内存失控。
constexpr int MaxHistoryStates = 40;
}

// 构造函数：以空文档的初始状态作为第一份历史状态
VpDocument::VpDocument(QObject *parent) : QObject(parent)
{
    resetHistory();
}

// 从文件加载背景图像，并清空平面、浮动图像与绘画层、重置历史记录
bool VpDocument::loadImage(const QString &fileName)
{
    QImageReader reader(fileName);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() * 4 > 128*1024*1024)
        return false;
    QImageReader::setAllocationLimit(128);
    const QImage image = reader.read().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull())
        return false;
    QImage painting(image.size(), QImage::Format_ARGB32);
    if (painting.isNull())
        return false;
    painting.fill(Qt::transparent);
    m_editActive = false;
    m_editBefore = HistoryEntry();
    m_editPaintBefore = QImage();
    clearPaintTransaction();
    m_background = image;
    m_planes.clear();
    m_floatingImages.clear();
    m_selectedPlane = -1;
    setSelectedFloatingImage(-1);
    m_paintLayer = painting;
    m_paintTransactionActive = false;
    resetHistory();
    emit documentAvailabilityChanged(true);
    return true;
}

// 分配一个新的展开曲面分组号（比现有最大分组号大 1）
int VpDocument::nextSurfaceGroupId() const
{
    int nextSurfaceGroup = 0;
    for (const PerspectivePlane &existing : m_planes)
        nextSurfaceGroup = qMax(nextSurfaceGroup, existing.surfaceGroupId() + 1);
    return nextSurfaceGroup;
}

// 绘画层是否含有任何不透明像素（测试与显式命令查询使用，避免逐帧调用）
bool VpDocument::hasPaintContent() const
{
    if (m_paintLayer.isNull())
        return false;
    for (int y = 0; y < m_paintLayer.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(m_paintLayer.constScanLine(y));
        for (int x = 0; x < m_paintLayer.width(); ++x) {
            if (qAlpha(line[x]) != 0)
                return true;
        }
    }
    return false;
}

// 清空绘画层（不影响平面几何与浮动图像）

// 开始一次绘画事务：浅拷贝当前绘画层作为撤销基线（隐式共享，O(1)）
void VpDocument::beginPaintTransaction()
{
    m_paintBefore = m_paintLayer;
    m_paintDirtyRect = QRect();
    m_paintTransactionActive = true;
}

// 累积本次绘画事务的脏矩形
void VpDocument::addPaintDirty(const QRect &rect)
{
    if (rect.isEmpty())
        return;
    m_paintDirtyRect = m_paintDirtyRect.isEmpty() ? rect : m_paintDirtyRect.united(rect);
}

// 追加一个平面并返回其下标；几何非法时不追加，返回 -1。
int VpDocument::appendPlane(const PerspectivePlane &plane)
{
    if (!plane.quad().isProjectable() || !qIsFinite(plane.angleToParentDegrees()))
        return -1;
    m_planes.append(plane);
    return m_planes.size() - 1;
}

bool VpDocument::setPlane(int index, const PerspectivePlane &plane)
{
    if (index < 0 || index >= m_planes.size() || !plane.quad().isProjectable() || !qIsFinite(plane.angleToParentDegrees()) || !m_planes[index].preservesLockedEdges(plane))
        return false;
    m_planes[index] = plane;
    return true;
}

bool VpDocument::setFloatingImage(int index, const FloatingImage &image)
{
    if (index < 0 || index >= m_floatingImages.size() || image.bitmap.isNull()
        || !qIsFinite(image.placementOrigin.x()) || !qIsFinite(image.placementOrigin.y())
        || !qIsFinite(image.rotationDegrees)
        || !qIsFinite(image.scaleFactors.x()) || !qIsFinite(image.scaleFactors.y())
        || image.scaleFactors.x() <= 0 || image.scaleFactors.y() <= 0)
        return false;
    if (image.surfaceAttached) {
        if (image.hostQuadIndex < 0 || image.hostQuadIndex >= image.surfaceQuads.size())
            return false;
        for (const PerspectiveQuad &quad : image.surfaceQuads)
            if (!quad.isProjectable())
                return false;
    }
    m_floatingImages[index] = image;
    return true;
}

// 删除指定平面。内容已与平面解耦：浮动图像持有自己的几何快照，
// 绘画层独立于平面，因此删除平面无需修正任何内容。
void VpDocument::removePlane(int index)
{
    if (index < 0 || index >= m_planes.size())
        return;
    // 删除前先解除共用边的锁定。这里必须依赖 parentPlaneIndex / parentEdgeIndex 这条
    // 显式关系，不能靠"两端点几何重合"来判断：子平面被缩放后（延长它的邻边会
    // 连带改变共用边的长度），共用边的端点已经不再与父平面重合，几何匹配会失效，
    // 父平面的锁定边就永远解不开——表现为删除子平面后，父平面共用边上的三个控制点
    // 仍锁死、平面也拖不动。
    const PerspectivePlane removed = m_planes[index];
    auto unlockEdge = [this](int planeIndex, int edge) {
        if (planeIndex >= 0 && planeIndex < m_planes.size() && edge >= 0 && edge < 4)
            m_planes[planeIndex].setEdgeLocked(edge, false);
    };
    // 被删的是子平面：解锁父平面上被它共用的那条边。
    bool stillShared = false;
    for (int other = 0; other < m_planes.size(); ++other)
        if (other != index && m_planes[other].parentPlaneIndex() == removed.parentPlaneIndex()
            && m_planes[other].parentEdgeIndex() == removed.parentEdgeIndex())
            stillShared = true;
    if (!stillShared)
        unlockEdge(removed.parentPlaneIndex(), removed.parentEdgeIndex());
    for (int other = 0; other < m_planes.size(); ++other) {
        if (other == index)
            continue;
        if (m_planes[other].parentPlaneIndex() == index) {
            // 被删的是父平面：子平面的第 0 条边就是共用边。
            unlockEdge(other, 0);
            m_planes[other].clearParent();
        } else if (m_planes[other].parentPlaneIndex() > index) {
            m_planes[other].setParent(m_planes[other].parentPlaneIndex() - 1,
                                      m_planes[other].parentEdgeIndex());
        }
    }
    m_planes.removeAt(index);
    if (m_selectedPlane > index)
        --m_selectedPlane;
    else if (m_selectedPlane == index)
        m_selectedPlane = m_planes.isEmpty() ? -1 : qMin(index, m_planes.size() - 1);
    if (!m_editActive)
        commitHistory();
}

// 追加一张浮动图像到画布左上角，返回其索引
int VpDocument::addFloatingImage(const QImage &image)
{
    if (image.isNull())
        return -1;
    FloatingImage floating;
    floating.bitmap = image.convertToFormat(QImage::Format_ARGB32);
    floating.placementOrigin = QPointF(0, 0);
    floating.surfaceAttached = false;
    floating.hostQuadIndex = -1;
    m_floatingImages.append(floating);
    setSelectedFloatingImage(m_floatingImages.size() - 1);
    commitHistory();
    return m_selectedFloatingImage;
}

int VpDocument::addFloatingImageOnSurface(const QImage &image,
                                               const QVector<PerspectiveQuad> &surfaceQuads,
                                               int hostQuadIndex,
                                               const QPointF &surfaceOrigin)
{
    if (image.isNull() || surfaceQuads.isEmpty()
        || hostQuadIndex < 0 || hostQuadIndex >= surfaceQuads.size()) {
        return -1;
    }
    FloatingImage floating;
    floating.bitmap = image.convertToFormat(QImage::Format_ARGB32);
    floating.placementOrigin = surfaceOrigin;
    floating.surfaceAttached = true;
    floating.surfaceQuads = surfaceQuads;
    floating.hostQuadIndex = hostQuadIndex;
    if (!qIsFinite(surfaceOrigin.x()) || !qIsFinite(surfaceOrigin.y()))
        return -1;
    for (const PerspectiveQuad &quad : surfaceQuads)
        if (!quad.isProjectable())
            return -1;
    m_floatingImages.append(floating);
    setSelectedFloatingImage(m_floatingImages.size() - 1);
    commitHistory();
    return m_selectedFloatingImage;
}

void VpDocument::lockPlaneEdge(int index, int edge)
{
    if (index >= 0 && index < m_planes.size() && edge >= 0 && edge < 4)
        m_planes[index].setEdgeLocked(edge, true);
}

// 是否与相邻垂直平面共边：自己是子平面，或是别的平面的父平面。
bool VpDocument::isPlaneLinked(int index) const
{
    if (index < 0 || index >= m_planes.size())
        return false;
    if (m_planes[index].parentPlaneIndex() >= 0)
        return true;
    for (const PerspectivePlane &plane : m_planes) {
        if (plane.parentPlaneIndex() == index)
            return true;
    }
    return false;
}

// 删除浮动图像，并将选中项移动到删除位置上的下一张（若无则为上一张）。
void VpDocument::removeFloatingImage(int index)
{
    if (index < 0 || index >= m_floatingImages.size())
        return;

    m_floatingImages.removeAt(index);
    int nextSelection = m_selectedFloatingImage;
    if (m_selectedFloatingImage == index)
        nextSelection = m_floatingImages.isEmpty() ? -1 : qMin(index, m_floatingImages.size() - 1);
    else if (m_selectedFloatingImage > index)
        --nextSelection;
    setSelectedFloatingImage(nextSelection);
    if (!m_editActive)
        commitHistory();
}

// 仅移动图像位置（不改变吸附状态）
void VpDocument::setSelectedFloatingImage(int index)
{
    index = index >= 0 && index < m_floatingImages.size() ? index : -1;
    if (m_selectedFloatingImage == index)
        return;
    m_selectedFloatingImage = index;
    emit imageSelectionChanged(index >= 0);
}

void VpDocument::setFloatingImageOrigin(int index, const QPointF &placementOrigin)
{
    if (index < 0 || index >= m_floatingImages.size())
        return;
    FloatingImage image = m_floatingImages[index];
    image.placementOrigin = placementOrigin;
    setFloatingImage(index, image);
}

// 把图像吸附到一组几何快照上（严格快照：此后平面增删改不再影响它）
void VpDocument::attachFloatingImage(int index,
                                         const QVector<PerspectiveQuad> &surfaceQuads,
                                         int hostQuadIndex, const QPointF &surfaceOrigin)
{
    if (index < 0 || index >= m_floatingImages.size())
        return;
    FloatingImage image = m_floatingImages[index];
    image.surfaceQuads = surfaceQuads;
    image.hostQuadIndex = hostQuadIndex;
    image.placementOrigin = surfaceOrigin;
    image.surfaceAttached = true;
    setFloatingImage(index, image);
}

// 让图像脱离曲面，回到画布坐标
void VpDocument::detachFloatingImage(int index, const QPointF &canvasOrigin)
{
    if (index < 0 || index >= m_floatingImages.size())
        return;
    FloatingImage image = m_floatingImages[index];
    image.surfaceQuads.clear();
    image.hostQuadIndex = -1;
    image.placementOrigin = canvasOrigin;
    image.surfaceAttached = false;
    setFloatingImage(index, image);
}

// 撤销：回退到上一状态（结构 + 绘画层脏矩形反演）
bool VpDocument::undo()
{
    if (m_historyIndex <= 0)
        return false;
    const HistoryEntry &leaving = m_history[m_historyIndex];
    --m_historyIndex;
    restoreStructure(m_history[m_historyIndex]);
    if (!leaving.paintRect.isEmpty())
        applyPaint(leaving.paintRect, leaving.paintBefore);
    emit canUndoChanged(m_historyIndex > 0);
    emit canRedoChanged(true);
    return true;
}

// 重做：前进到下一状态
bool VpDocument::redo()
{
    if (m_historyIndex + 1 >= m_history.size())
        return false;
    ++m_historyIndex;
    const HistoryEntry &target = m_history[m_historyIndex];
    restoreStructure(target);
    if (!target.paintRect.isEmpty())
        applyPaint(target.paintRect, target.paintAfter);
    emit canUndoChanged(true);
    emit canRedoChanged(m_historyIndex + 1 < m_history.size());
    return true;
}

// 清空历史并以当前状态作为初始状态（用于加载新文档）
void VpDocument::resetHistory()
{
    m_editActive = false;
    m_editBefore = HistoryEntry();
    m_editPaintBefore = QImage();
    m_history.clear();
    HistoryEntry initial;
    initial = captureStructure();
    m_history.append(initial);
    m_historyIndex = 0;
    clearPaintTransaction();
    emit canUndoChanged(false);
    emit canRedoChanged(false);
}

// 提交一次状态变更：丢弃旧的重做分支，追加新状态并裁剪历史长度
void VpDocument::beginEdit()
{
    if (m_editActive)
        return;
    m_editBefore = captureStructure();
    m_editPaintBefore = m_paintLayer;
    m_editActive = true;
}

void VpDocument::commitEdit(bool changed)
{
    if (!m_editActive)
        return;
    m_editActive = false;
    m_editBefore = HistoryEntry();
    m_editPaintBefore = QImage();
    if (changed)
        commitHistory();
    else {
        m_paintTransactionActive = false;
        m_paintBefore = QImage();
        m_paintDirtyRect = QRect();
    }
}

void VpDocument::cancelEdit()
{
    if (!m_editActive)
        return;
    // 恢复选中会同步通知 UI，先结束事务以避免信号重入。
    m_editActive = false;
    const HistoryEntry before = m_editBefore;
    m_paintLayer = m_editPaintBefore;
    m_editBefore = HistoryEntry();
    m_editPaintBefore = QImage();
    m_paintBefore = QImage();
    m_paintDirtyRect = QRect();
    m_paintTransactionActive = false;
    restoreStructure(before);
}

void VpDocument::commitHistory()
{
    HistoryEntry entry;
    entry = captureStructure();

    // 绘画层只记录本次变动的脏矩形前后像素
    if (m_paintTransactionActive && !m_paintDirtyRect.isEmpty()) {
        const QRect dirty = m_paintDirtyRect.intersected(m_paintLayer.rect());
        if (!dirty.isEmpty()) {
            entry.paintRect = dirty;
            entry.paintBefore = m_paintBefore.copy(dirty);
            entry.paintAfter = m_paintLayer.copy(dirty);
        }
    }
    clearPaintTransaction();

    const HistoryEntry &current = m_history[m_historyIndex];
    if (entry.planes == current.planes && entry.floatingImages == current.floatingImages
        && (entry.paintRect.isEmpty() || entry.paintBefore == entry.paintAfter))
        return;
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(entry);
    ++m_historyIndex;
    auto historyBytes = [this] {
        qint64 bytes = 0;
        QSet<qint64> counted;
        for (const FloatingImage &image : m_floatingImages)
            counted.insert(image.bitmap.cacheKey());
        for (const HistoryEntry &state : m_history) {
            bytes += state.paintBefore.sizeInBytes() + state.paintAfter.sizeInBytes();
            for (const FloatingImage &image : state.floatingImages) {
                if (!counted.contains(image.bitmap.cacheKey())) {
                    counted.insert(image.bitmap.cacheKey());
                    bytes += image.bitmap.sizeInBytes();
                }
            }
        }
        return bytes;
    };
    while (m_history.size() > MaxHistoryStates
           || (m_history.size() > 2 && historyBytes() > 128*1024*1024)) {
        m_history.removeFirst();
        m_history.first().paintBefore = QImage();
        m_history.first().paintAfter = QImage();
        m_history.first().paintRect = QRect();
        --m_historyIndex;
    }
    emit canUndoChanged(m_historyIndex > 0);
    emit canRedoChanged(false);
}

// 仅恢复结构部分（平面 + 浮动图像 + 选中状态）
void VpDocument::restoreStructure(const HistoryEntry &entry)
{
    m_planes = entry.planes;
    m_selectedPlane = entry.selectedPlane;
    m_floatingImages = entry.floatingImages;
    setSelectedFloatingImage(entry.selectedFloatingImage);
}

// 把像素直接覆盖回绘画层的指定矩形（用于脏矩形的撤销/重做）
void VpDocument::applyPaint(const QRect &rect, const QImage &pixels)
{
    if (rect.isEmpty() || pixels.isNull())
        return;
    QPainter painter(&m_paintLayer);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(rect.topLeft(), pixels);
}

VpDocument::HistoryEntry VpDocument::captureStructure() const
{
    HistoryEntry entry;
    entry.planes = m_planes;
    entry.selectedPlane = m_selectedPlane;
    entry.floatingImages = m_floatingImages;
    entry.selectedFloatingImage = m_selectedFloatingImage;
    return entry;
}

void VpDocument::clearPaintTransaction()
{
    m_paintTransactionActive = false;
    m_paintBefore = QImage();
    m_paintDirtyRect = QRect();
}
