#pragma once
#include "../core/floatingselectionprojection.h"
#include "../core/scenerenderer.h"
#include <QQuickPaintedItem>
#include <QDataStream>
#include <QIODevice>
#include <QPainter>
#include <functional>
#include <atomic>
#include <QtMath>

// A cropped image layer, preserving Qt's existing projective sampling and seams.
// Each floating selection retains its own texture while other selections move.
class FloatingContentItem final : public QQuickPaintedItem
{
public:
    explicit FloatingContentItem(QQuickItem *parent) : QQuickPaintedItem(parent)
    { setAcceptedMouseButtons(Qt::NoButton); setAntialiasing(true); setVisible(false); }
    void setImages(const QVector<FloatingSelection> &images, qreal scale,
                   const QPointF &offset, const QSizeF &viewport,
                   const QSize &canvasSize = QSize())
    {
        QByteArray key; QDataStream stream(&key, QIODevice::WriteOnly);
        stream << scale << offset << viewport << canvasSize << qint64(images.size());
        for (const auto &image : images)
            stream << image.bitmap.cacheKey() << image.opacity << FloatingSelectionProjection::cacheKey(image);
        if (key == m_key) return;
        m_key = key; m_images = images; m_scale = scale; m_offset = offset;
        m_canvasSize = canvasSize;
        QRectF bounds;
        for (const auto &image : images) {
            const QRectF canvasBounds = FloatingSelectionProjection::forSelection(image)->canvasPaintOutline().boundingRect();
            const QRectF viewBounds(offset + canvasBounds.topLeft() * scale, canvasBounds.size() * scale);
            bounds = bounds.isEmpty() ? viewBounds : bounds.united(viewBounds);
        }
        if (!canvasSize.isEmpty())
            bounds = bounds.intersected(QRectF(offset, QSizeF(canvasSize) * scale));
        const QRect clipped = bounds.adjusted(-2,-2,2,2).intersected(QRectF(QPointF(), viewport)).toAlignedRect();
        setPosition(clipped.topLeft()); setSize(clipped.size());
        setVisible(!images.isEmpty() && !bounds.isEmpty() && !clipped.isEmpty()); update();
    }
    void paint(QPainter *painter) override
    {
        ++m_paints;
        painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter->translate(m_offset - position()); painter->scale(m_scale, m_scale);
        if (!m_canvasSize.isEmpty())
            painter->setClipRect(QRectF(QPointF(), m_canvasSize), Qt::IntersectClip);
        for (const auto &image : m_images)
            SceneRenderer::renderFloatingSelection(*painter, image, m_scale);
    }
    quint64 paintCount() const { return m_paints.load(); }
private:
    QByteArray m_key;
    QVector<FloatingSelection> m_images;
    qreal m_scale = 1;
    QPointF m_offset;
    QSize m_canvasSize;
    std::atomic<quint64> m_paints{0};
};

class CursorPreviewItem final : public QQuickPaintedItem
{
public:
    explicit CursorPreviewItem(QQuickItem *parent) : QQuickPaintedItem(parent)
    { setAcceptedMouseButtons(Qt::NoButton); setAntialiasing(true); setVisible(false); }
    std::function<void(QPainter *)> render;
    void paint(QPainter *painter) override { if (render) render(painter); }
};

// Reads the live paint layer only while Qt blocks the GUI thread for painting.
// Keeping a QImage snapshot here would detach the entire layer on the next dab.
class PaintLayerItem final : public QQuickPaintedItem
{
public:
    explicit PaintLayerItem(QQuickItem *parent) : QQuickPaintedItem(parent)
    { setAcceptedMouseButtons(Qt::NoButton); setAntialiasing(true); setVisible(false); }
    std::function<void(QPainter *)> render;
    void paint(QPainter *painter) override
    {
        ++m_paints;
        const QRectF clip = painter->hasClipping() ? painter->clipBoundingRect().intersected(boundingRect()) : boundingRect();
        m_area.fetch_add(quint64(qCeil(clip.width()) * qCeil(clip.height())));
        if (render) render(painter);
    }
    quint64 paintCount() const { return m_paints.load(); }
    quint64 paintedArea() const { return m_area.load(); }
private:
    std::atomic<quint64> m_paints{0}, m_area{0};
};
