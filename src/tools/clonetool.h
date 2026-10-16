#pragma once

#include "../core/perspectiveplane.h"
#include "../core/healingblend.h"

#include <QImage>
#include <QPainterPath>
#include <QRect>
#include <functional>
#include <QCache>
#include <array>

class QPainter;

class CloneTool
{
public:
    void setDiameter(int value) { m_diameter = qBound(1, value, 500); }
    void setHardness(int value) { m_hardness = qBound(0, value, 100); }
    void setOpacity(int value) { m_opacity = qBound(1, value, 100); }
    int diameter() const { return m_diameter; }
    int hardness() const { return m_hardness; }
    int opacity() const { return m_opacity; }
    void setAligned(bool value);
    bool aligned() const { return m_aligned; }
    int healMode() const { return m_healMode; }
    void setHealMode(int mode) { m_healMode = qBound(0, mode, 2); }

    void resetSource();
    bool hasSource() const { return m_hasSource; }
    bool drawing() const { return m_drawing; }
    void setSelectionClip(const QPainterPath &clip) { m_selectionClip = clip; }
    QPointF marker() const { return m_marker; }

    // 取源点：解析点所在面片并记下它在展开曲面上的位置。
    bool pickSource(const QVector<PerspectivePlane> &planes, const QSize &canvasSize, const QPointF &point);

    // 起笔：解析落点面片，按对齐模式确定偏移并落下第一个笔触点，返回画布脏矩形。
    // 未取源、面片不可解析或绘画层无效时返回空矩形，且不进入落笔状态。
    QRect begin(QImage &layer, const QImage &source, const QVector<PerspectivePlane> &planes,
                const QSize &canvasSize, const QPointF &point,
                const std::function<void(const QRect &)> &prepare = {});
    // 续笔：从上一位置向目标位置补间落点，返回画布脏矩形。
    QRect move(QImage &layer, const QPointF &point, const std::function<void(const QRect &)> &prepare = {});
    // 鼠标松开时统一修复本笔；不传 layer 时仅清理交互状态。
    QRect end(QImage *layer = nullptr);

    // 光标移动：更新取样位置指示。源点已归位时只显示它本身。
    void hover(const QVector<PerspectivePlane> &planes, const QSize &canvasSize, const QPointF &point);
    // 在光标处预览即将仿制过来的内容（所见即所得）：复用逐像素取样，
    // 与真实落笔完全一致，取样位置始终落在源点十字上，但不改变源点与落笔状态。
    void renderPreview(QPainter &painter, const QImage &source, const QVector<PerspectivePlane> &planes,
                       const QSize &canvasSize, const QPointF &point);
    QRectF previewBounds(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                         const QPointF &point) const;

private:
    struct StampContext {
        QImage source;
        PerspectiveTransform targetToCanvas, sourceToCanvas;
        QPointF offset;
        QByteArray coordinatesKey;
    };

    // 目标面片的展开映射：落笔期间沿用锚定面片，否则取光标所在面片。
    PerspectiveTransform targetAt(const QVector<PerspectivePlane> &planes, const QSize &canvasSize,
                                 const QPointF &point) const;
    // 本次落笔应有的偏移：对齐模式沿用已锁定的偏移，其余按落点重新锚定。
    QPointF anchoredOffset(const QPointF &position) const;
    QPointF originalMarker() const; // 未加偏移时的源点位置

    static StampContext makeStampContext(const QImage &source, const PerspectiveTransform &targetToCanvas,
                                         const PerspectiveTransform &sourceToCanvas, const QPointF &offset);
    QRect dabRect(const StampContext &context, const QPointF &position) const;
    bool renderDab(QImage &dab, const QRect &area, const QPointF &position,
                   const StampContext &context) const;
    QRect applyDab(QImage &layer, const QPointF &position, const std::function<void(const QRect &)> &prepare);

    StampContext m_stamp;
    QImage m_dab, m_previewDab;
    static constexpr int CoordinateTileSize = 32;
    static constexpr int CoordinateTileCount = 128;
    struct CoordinateTile {
        std::array<QPointF, CoordinateTileSize*CoordinateTileSize> target, source;
        // 0: unknown; 1: invalid target; 2: target known; 3: source known; 4: invalid source.
        std::array<quint8, CoordinateTileSize*CoordinateTileSize> state{};
    };
    mutable QCache<quint64, CoordinateTile> m_coordinateTiles{CoordinateTileCount};
    mutable QByteArray m_coordinatesKey;
    QPointF m_lastPosition;
    int m_diameter = 42;
    int m_hardness = 75;
    int m_opacity = 100;
    int m_healMode = HealingBlend::On;
    struct Dab { QPointF position; int diameter, hardness, opacity; };
    QVector<Dab> m_healDabs;
    QImage m_beforeHealStroke;
    QRect m_healDirty;
    bool m_finalizingHeal = false;
    PerspectiveTransform m_sourceMapping, m_targetMapping;
    bool m_hasSource = false, m_hasOffset = false, m_aligned = false, m_drawing = false;
    QPainterPath m_selectionClip;
    QPointF m_source; // 源点在展开曲面上的位置
    QPointF m_offset; // 源相对落点的展开坐标偏移
    QPointF m_marker; // 画面上的取样位置指示
};
