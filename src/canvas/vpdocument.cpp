#include "vpdocument.h"

#include <QPainter>
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

bool VpDocument::setImage(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
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
    m_floatingSelections.clear();
    m_selectedPlane = -1;
    setSelectedFloatingSelection(-1);
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

// 清空绘画层（不影响平面几何与浮动选区）

// 传统调用保留 COW 快照；画笔在每个 dab 写入前保存首次触及的块，避免整层 detach。
void VpDocument::beginPaintTransaction(bool incremental)
{
    clearPaintTransaction();
    m_incrementalPaintTransaction = incremental;
    if (!incremental) m_paintBefore = m_paintLayer;
    m_paintDirtyRect = QRect();
    m_paintTransactionActive = true;
}

void VpDocument::preparePaintDirty(const QRect &rect)
{
    if (!m_paintTransactionActive || !m_incrementalPaintTransaction) return;
    const QRect dirty = rect.intersected(m_paintLayer.rect());
    if (dirty.isEmpty()) return;
    constexpr int tileSize = 256;
    const int columns = (m_paintLayer.width() + tileSize - 1) / tileSize;
    for (int y = dirty.top() / tileSize; y <= dirty.bottom() / tileSize; ++y)
        for (int x = dirty.left() / tileSize; x <= dirty.right() / tileSize; ++x) {
            const int key = y * columns + x;
            if (!m_paintBeforeTiles.contains(key))
                m_paintBeforeTiles.insert(key, m_paintLayer.copy(QRect(x*tileSize, y*tileSize, tileSize, tileSize)));
        }
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
    emit structureChanged();
    return m_planes.size() - 1;
}

bool VpDocument::setPlane(int index, const PerspectivePlane &plane, bool allowSharedEdgeExtension)
{
    if (index < 0 || index >= m_planes.size() || !plane.quad().isProjectable()
        || !qIsFinite(plane.angleToParentDegrees())
        || !m_planes[index].preservesLockedEdges(plane, allowSharedEdgeExtension))
        return false;
    m_planes[index] = plane;
    return true;
}

bool VpDocument::setFloatingSelection(int index, const FloatingSelection &image)
{
    if (index < 0 || index >= m_floatingSelections.size() || image.bitmap.isNull()
        || !qIsFinite(image.placementOrigin.x()) || !qIsFinite(image.placementOrigin.y())
        || !qIsFinite(image.rotationDegrees)
        || !qIsFinite(image.scaleFactors.x()) || !qIsFinite(image.scaleFactors.y())
        || image.scaleFactors.x() <= 0 || image.scaleFactors.y() <= 0)
        return false;
    if (image.opacity < 0 || image.opacity > 100 || image.healMode < 0 || image.healMode > 2)
        return false;
    if (image.surfaceAttached) {
        if (image.hostQuadIndex < 0 || image.hostQuadIndex >= image.surfaceQuads.size())
            return false;
        for (const PerspectiveQuad &quad : image.surfaceQuads)
            if (!quad.isProjectable())
                return false;
    }
    m_floatingSelections[index] = image;
    return true;
}

// 删除指定平面。内容已与平面解耦：浮动选区持有自己的几何快照，
// 绘画层独立于平面，因此删除平面无需修正任何内容。
void VpDocument::removePlane(int index)
{
    if (index < 0 || index >= m_planes.size())
        return;
    QVector<bool> removed(m_planes.size(), false);
    QVector<int> subtree{index};
    removed[index] = true;
    // Follow explicit parent links, including grandchildren and deeper descendants.
    for (int cursor = 0; cursor < subtree.size(); ++cursor) {
        for (int other = 0; other < m_planes.size(); ++other) {
            if (!removed[other] && m_planes[other].parentPlaneIndex() == subtree[cursor]) {
                removed[other] = true;
                subtree.append(other);
            }
        }
    }
    // Unlock surviving parents only if no surviving child still shares that edge.
    for (int child : subtree) {
        const int parent = m_planes[child].parentPlaneIndex();
        const int edge = m_planes[child].parentEdgeIndex();
        if (parent < 0 || parent >= m_planes.size() || removed[parent]) continue;
        bool stillShared = false;
        for (int other = 0; other < m_planes.size(); ++other) {
            if (!removed[other] && m_planes[other].parentPlaneIndex() == parent
                && m_planes[other].parentEdgeIndex() == edge) {
                stillShared = true;
                break;
            }
        }
        if (!stillShared) m_planes[parent].setEdgeLocked(edge, false);
    }
    QVector<int> newIndices(m_planes.size(), -1);
    QVector<PerspectivePlane> surviving;
    for (int old = 0; old < m_planes.size(); ++old) {
        if (!removed[old]) {
            newIndices[old] = surviving.size();
            surviving.append(m_planes[old]);
        }
    }
    for (PerspectivePlane &plane : surviving) {
        const int parent = plane.parentPlaneIndex();
        if (parent >= 0 && parent < newIndices.size())
            plane.setParent(newIndices[parent], plane.parentEdgeIndex());
    }
    if (m_selectedPlane >= 0 && m_selectedPlane < newIndices.size()) {
        if (newIndices[m_selectedPlane] >= 0) {
            m_selectedPlane = newIndices[m_selectedPlane];
        } else {
            int before = 0;
            for (int old = 0; old < m_selectedPlane; ++old)
                if (!removed[old]) ++before;
            m_selectedPlane = surviving.isEmpty() ? -1 : qMin(before, surviving.size() - 1);
        }
    }
    m_planes = std::move(surviving);
    if (!m_editActive)
        commitHistory();
}

// 追加浮动选区快照，保留复制时的透视、位置、缩放和旋转。
int VpDocument::addFloatingSelection(const FloatingSelection &selection)
{
    const int index = m_floatingSelections.size();
    m_floatingSelections.append(selection);
    if (!setFloatingSelection(index, selection)) {
        m_floatingSelections.removeLast();
        return -1;
    }
    setSelectedFloatingSelection(m_floatingSelections.size() - 1);
    commitHistory();
    return m_selectedFloatingSelection;
}

int VpDocument::addFloatingSelectionOnSurface(const QImage &image,
                                               const QVector<PerspectiveQuad> &surfaceQuads,
                                               int hostQuadIndex,
                                               const QPointF &surfaceOrigin)
{
    if (image.isNull() || surfaceQuads.isEmpty()
        || hostQuadIndex < 0 || hostQuadIndex >= surfaceQuads.size()) {
        return -1;
    }
    FloatingSelection floating;
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
    m_floatingSelections.append(floating);
    setSelectedFloatingSelection(m_floatingSelections.size() - 1);
    commitHistory();
    return m_selectedFloatingSelection;
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

// 删除浮动选区，并将选中项移动到删除位置上的下一张（若无则为上一张）。
void VpDocument::removeFloatingSelection(int index)
{
    if (index < 0 || index >= m_floatingSelections.size())
        return;

    m_floatingSelections.removeAt(index);
    int nextSelection = m_selectedFloatingSelection;
    if (m_selectedFloatingSelection == index)
        nextSelection = m_floatingSelections.isEmpty() ? -1 : qMin(index, m_floatingSelections.size() - 1);
    else if (m_selectedFloatingSelection > index)
        --nextSelection;
    setSelectedFloatingSelection(nextSelection);
    if (!m_editActive)
        commitHistory();
}

// 仅移动图像位置（不改变吸附状态）
void VpDocument::setSelectedFloatingSelection(int index)
{
    index = index >= 0 && index < m_floatingSelections.size() ? index : -1;
    if (m_selectedFloatingSelection == index)
        return;
    m_selectedFloatingSelection = index;
    emit floatingSelectionChanged(index >= 0);
}

void VpDocument::setFloatingSelectionOrigin(int index, const QPointF &placementOrigin)
{
    if (index < 0 || index >= m_floatingSelections.size())
        return;
    FloatingSelection image = m_floatingSelections[index];
    image.placementOrigin = placementOrigin;
    setFloatingSelection(index, image);
}

// 把图像吸附到一组几何快照上（严格快照：此后平面增删改不再影响它）
void VpDocument::attachFloatingSelection(int index,
                                         const QVector<PerspectiveQuad> &surfaceQuads,
                                         int hostQuadIndex, const QPointF &surfaceOrigin)
{
    if (index < 0 || index >= m_floatingSelections.size())
        return;
    FloatingSelection image = m_floatingSelections[index];
    image.surfaceQuads = surfaceQuads;
    image.hostQuadIndex = hostQuadIndex;
    image.placementOrigin = surfaceOrigin;
    image.surfaceAttached = true;
    setFloatingSelection(index, image);
}

// 让图像脱离曲面，回到画布坐标
void VpDocument::detachFloatingSelection(int index, const QPointF &canvasOrigin)
{
    if (index < 0 || index >= m_floatingSelections.size())
        return;
    FloatingSelection image = m_floatingSelections[index];
    image.surfaceQuads.clear();
    image.hostQuadIndex = -1;
    image.placementOrigin = canvasOrigin;
    image.surfaceAttached = false;
    setFloatingSelection(index, image);
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
    emit structureChanged();
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
        clearPaintTransaction();
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
    clearPaintTransaction();
    restoreStructure(before);
}

void VpDocument::commitHistory()
{
    // An edit owns its history entry until commitEdit() confirms the transaction.
    if (m_editActive)
        return;
    HistoryEntry entry;
    entry = captureStructure();

    // 绘画层只记录本次变动的脏矩形前后像素
    if (m_paintTransactionActive && !m_paintDirtyRect.isEmpty()) {
        const QRect dirty = m_paintDirtyRect.intersected(m_paintLayer.rect());
        if (!dirty.isEmpty()) {
            entry.paintRect = dirty;
            entry.paintAfter = m_paintLayer.copy(dirty);
            if (m_incrementalPaintTransaction) {
                // Pixels outside changed tiles are unchanged, so the after image is a valid base.
                entry.paintBefore = entry.paintAfter.copy();
                QPainter before(&entry.paintBefore);
                before.setCompositionMode(QPainter::CompositionMode_Source);
                constexpr int tileSize = 256;
                const int columns = (m_paintLayer.width() + tileSize - 1) / tileSize;
                for (auto tile = m_paintBeforeTiles.cbegin(); tile != m_paintBeforeTiles.cend(); ++tile)
                    before.drawImage(QPoint((tile.key()%columns)*tileSize, (tile.key()/columns)*tileSize) - dirty.topLeft(), tile.value());
            } else entry.paintBefore = m_paintBefore.copy(dirty);
        }
    }
    clearPaintTransaction();

    const HistoryEntry &current = m_history[m_historyIndex];
    if (entry.planes == current.planes && entry.floatingSelections == current.floatingSelections
        && (entry.paintRect.isEmpty() || entry.paintBefore == entry.paintAfter))
        return;
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(entry);
    ++m_historyIndex;
    auto historyBytes = [this] {
        qint64 bytes = 0;
        QSet<qint64> counted;
        for (const FloatingSelection &image : m_floatingSelections)
            counted.insert(image.bitmap.cacheKey());
        for (const FloatingSelection &image : m_floatingSelections)
            counted.insert(image.unhealedBitmap.cacheKey());
        for (const HistoryEntry &state : m_history) {
            bytes += state.paintBefore.sizeInBytes() + state.paintAfter.sizeInBytes();
            for (const FloatingSelection &image : state.floatingSelections) {
                if (!counted.contains(image.bitmap.cacheKey())) {
                    counted.insert(image.bitmap.cacheKey());
                    bytes += image.bitmap.sizeInBytes();
                }
                if (!counted.contains(image.unhealedBitmap.cacheKey())) {
                    counted.insert(image.unhealedBitmap.cacheKey());
                    bytes += image.unhealedBitmap.sizeInBytes();
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
    emit structureChanged();
}

// 仅恢复结构部分（平面 + 浮动选区 + 选中状态）
void VpDocument::restoreStructure(const HistoryEntry &entry)
{
    m_planes = entry.planes;
    m_selectedPlane = entry.selectedPlane;
    m_floatingSelections = entry.floatingSelections;
    setSelectedFloatingSelection(entry.selectedFloatingSelection);
    emit structureChanged();
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
    entry.floatingSelections = m_floatingSelections;
    entry.selectedFloatingSelection = m_selectedFloatingSelection;
    return entry;
}

void VpDocument::clearPaintTransaction()
{
    m_paintTransactionActive = false;
    m_paintBefore = QImage();
    m_paintBeforeTiles.clear();
    m_incrementalPaintTransaction = false;
    m_paintDirtyRect = QRect();
}
