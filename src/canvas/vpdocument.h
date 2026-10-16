#pragma once

#include "../core/floatingselection.h"
#include "../core/perspectiveplane.h"

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QHash>

// 文档模型：管理背景图像、全部透视平面、绘画层、浮动选区数组以及
// 撤销/重做历史。它是纯数据与历史的集合体，不涉及视图、渲染或交互细节。
//
// 内容与平面解耦的核心约定：
//  - 平面只提供透视规则（几何），不持有绘画或图像内容；
//  - 画笔笔触烘焙在画布同尺寸的绘画层上（画布坐标）；
//  - 浮动选区各自携带吸附瞬间的几何快照，严格独立于平面。
class VpDocument : public QObject
{
    Q_OBJECT
public:
    explicit VpDocument(QObject *parent = nullptr);

    // —— 背景（即“文档”） ——
    bool setImage(const QImage &image);            // 从画布合成图初始化独立的预览文档
    const QImage &background() const { return m_background; }
    bool hasLoadedImage() const { return !m_background.isNull(); }

    // —— 平面 ——
    const QVector<PerspectivePlane> &planes() const { return m_planes; }
    int appendPlane(const PerspectivePlane &plane);           // 几何预览修改由 beginEdit/commitEdit 包围
    bool setPlane(int index, const PerspectivePlane &plane, bool allowSharedEdgeExtension = false);
    void lockPlaneEdge(int index, int edge);
    bool isPlaneLinked(int index) const;           // 是否与相邻垂直平面共边
    int selectedPlane() const { return m_selectedPlane; }
    void setSelectedPlane(int index) { m_selectedPlane = index >= 0 && index < m_planes.size() ? index : -1; }
    int nextSurfaceGroupId() const;                // 分配一个新的展开曲面分组号
    void removePlane(int index);                   // 删除平面（不影响任何已存在内容）

    // —— 绘画层（画布同尺寸，画笔笔触烘焙于此） ——
    QImage &paintLayer() { return m_paintLayer; }
    const QImage &paintLayer() const { return m_paintLayer; }
    bool hasPaintContent() const;                  // 绘画层是否含有不透明像素
    void beginPaintTransaction(bool incremental = false); // 笔刷可按首次触及的块记录 before
    void preparePaintDirty(const QRect &rect);     // 增量事务：必须在写像素前调用
    void addPaintDirty(const QRect &rect);         // 累积绘画脏矩形

    // —— 浮动选区 ——
    const QVector<FloatingSelection> &floatingSelections() const { return m_floatingSelections; }
    const FloatingSelection &floatingSelection(int index) const { return m_floatingSelections[index]; }
    bool setFloatingSelection(int index, const FloatingSelection &image);
    int selectedFloatingSelection() const { return m_selectedFloatingSelection; }
    void setSelectedFloatingSelection(int index);
    int addFloatingSelection(const FloatingSelection &selection); // 追加复制快照，返回索引
    int addFloatingSelectionOnSurface(const QImage &image,
                                  const QVector<PerspectiveQuad> &surfaceQuads,
                                  int hostQuadIndex, const QPointF &surfaceOrigin);
    void removeFloatingSelection(int index);          // 删除指定浮动选区
    void setFloatingSelectionOrigin(int index, const QPointF &placementOrigin); // 仅移动位置
    // 把图像吸附到一组几何快照上（surfacePosition 为展开曲面坐标）
    void attachFloatingSelection(int index, const QVector<PerspectiveQuad> &surfaceQuads,
                             int hostQuadIndex, const QPointF &surfaceOrigin);
    void detachFloatingSelection(int index, const QPointF &canvasOrigin);

    // —— 历史 ——
    bool undo();                                   // 回退到上一状态，返回是否发生了撤销
    bool redo();                                   // 前进到下一状态，返回是否发生了重做
    bool canUndo() const { return m_historyIndex > 0; }
    bool canRedo() const { return m_historyIndex + 1 < m_history.size(); }
    void resetHistory();                           // 清空历史并以当前状态为初始状态
    void commitHistory();                          // 提交一次状态变更到历史
    // 交互事务只保存本次操作前的 COW 快照，历史仍使用绘画脏矩形增量。
    void beginEdit();
    void commitEdit(bool changed);
    void cancelEdit();
    bool editActive() const { return m_editActive; }

signals:
    void structureChanged();
    void floatingSelectionChanged(bool selected);
    void canUndoChanged(bool available);           // 撤销可用性变化
    void canRedoChanged(bool available);           // 重做可用性变化
    void documentAvailabilityChanged(bool available); // 文档加载状态变化

private:
    // 历史条目：结构（平面 + 浮动选区）整份快照 + 绘画层的脏矩形增量。
    // 结构部分体积小、位图靠隐式共享，可直接整份拷贝；而绘画层整份快照
    // 在 40 步历史下会因整层逐笔复制而失控，故只记录本次变动的脏矩形前后像素。
    struct HistoryEntry {
        QVector<PerspectivePlane> planes;
        int selectedPlane = -1;
        QVector<FloatingSelection> floatingSelections;
        int selectedFloatingSelection = -1;
        QRect paintRect;              // 绘画层脏区域（空=本次无绘画变更）
        QImage paintBefore;           // 脏区域旧像素
        QImage paintAfter;            // 脏区域新像素
    };

    HistoryEntry captureStructure() const;
    void clearPaintTransaction();
    void restoreStructure(const HistoryEntry &entry); // 仅恢复结构部分
    void applyPaint(const QRect &rect, const QImage &pixels); // 把像素写回绘画层

    QImage m_background;                // 背景图像（即“文档”）
    QVector<PerspectivePlane> m_planes;            // 全部透视平面
    int m_selectedPlane = -1;           // 当前选中的平面索引
    QImage m_paintLayer;                // 绘画层（画布同尺寸）
    QVector<FloatingSelection> m_floatingSelections;    // 全部浮动选区
    int m_selectedFloatingSelection = -1;           // 当前操作的浮动选区索引
    QImage m_paintBefore;               // 绘画事务开始时的绘画层浅拷贝
    QHash<int, QImage> m_paintBeforeTiles; // 增量事务中首次修改前的 256×256 像素块
    bool m_incrementalPaintTransaction = false;
    QRect m_paintDirtyRect;             // 当前绘画事务累积的脏矩形
    bool m_paintTransactionActive = false; // 是否存在进行中的绘画事务
    // m_historyIndex 指向当前可见的状态。撤销之后的新的编辑会丢弃旧的
    // 重做分支，与主流编辑器的行为一致。
    QVector<HistoryEntry> m_history;    // 历史条目栈
    int m_historyIndex = -1;            // 当前状态在历史栈中的索引
    bool m_editActive = false;
    HistoryEntry m_editBefore;
    QImage m_editPaintBefore;
};
