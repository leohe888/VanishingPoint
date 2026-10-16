#include "vpcanvas.h"
#include "auxiliaryoverlayitem.h"
#include "floatingcontentitem.h"

#include "../core/floatingselectionprojection.h"
#include "../core/scenerenderer.h"

#include <QCursor>
#include <QImageReader>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QTransform>
#include <cmath>
#include <array>

namespace {
constexpr std::array<qreal,15> ZoomLevels{0.063,0.125,0.25,0.333,0.5,0.667,1,2,3,4,6,8,10,12,16};
constexpr qreal ViewMargin = 16.0;
}

VpCanvas::VpCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
    setAntialiasing(true);
    setCursor(Qt::ArrowCursor);
    m_auxiliaryOverlay = new AuxiliaryOverlayItem(this);
    m_paintLayerItem = new PaintLayerItem(this); m_paintLayerItem->setZ(.1);
    m_paintLayerItem->render = [this](QPainter *painter) {
        if (!m_controller) return;
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->translate(m_offset); painter->scale(m_scale, m_scale);
        painter->drawImage(QPointF(), document().paintLayer());
    };
    m_selectionPreview = new FloatingContentItem(this); m_selectionPreview->setZ(.25);
    m_cursorPreview = new CursorPreviewItem(this); m_cursorPreview->setZ(.75);
    m_cursorPreview->render = [this](QPainter *painter) {
        if (!m_controller) return;
        painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter->translate(m_offset - m_cursorPreview->position()); painter->scale(m_scale, m_scale);
        if (m_cursorOnCanvas && m_controller->tool() == Tool::Brush)
            m_controller->renderBrushPreview(*painter, m_cursorPoint);
        if (m_controller->tool() == Tool::CloneStamp) {
            if (m_cursorOnCanvas) m_controller->renderClonePreview(*painter, m_cursorPoint);
        }
    };
    m_cloneMarkerItem = new CursorPreviewItem(this); m_cloneMarkerItem->setZ(.8);
    m_cloneMarkerItem->render = [this](QPainter *painter) {
        if (!m_controller) return;
        painter->translate(m_offset - m_cloneMarkerItem->position()); painter->scale(m_scale, m_scale);
        drawCloneMarker(painter);
    };

    updateViewTransform();
}

void VpCanvas::setController(VpController *controller)
{
    if (m_controller == controller)
        return;
    disconnect(m_documentConnection);
    if (m_controller)
        disconnect(m_controller.data(), nullptr, this, nullptr);    // 断开 m_controller 到自己的所有连接
    m_controller = controller;
    if (m_controller) {
        connect(controller, &VpController::toolChanged, this, [this] {
            setSpaceHeld(false);
            m_panning = false;
            m_toolPointerActive = false;
            updateNavigationCursor(QGuiApplication::keyboardModifiers() & Qt::AltModifier);
            updateAuxiliaryOverlay();
        });
        connect(controller, &VpController::repaintRequested, this, [this] {
            updateContentLayers();
            updateAuxiliaryOverlay();
            updateBaseContent();
        });
        connect(controller, &VpController::guidesRepaintRequested,
                this, &VpCanvas::updateAuxiliaryOverlay);
        connect(controller, &VpController::paintRepaintRequested, this, &VpCanvas::updatePaintRegion);
        connect(controller, &VpController::brushChanged, this, &VpCanvas::updateCursorPreview);
        connect(controller, &VpController::cloneChanged, this, &VpCanvas::updateCursorPreview);
        connect(controller, &VpController::previewRepaintRequested, this, [this] {
            updateContentLayers(); updateAuxiliaryOverlay();
        });
        m_documentConnection = connect(&controller->document(), &VpDocument::structureChanged,
                                       this, &VpCanvas::updateContentLayers);
        connect(controller, &VpController::focusRequested, this, [this] { forceActiveFocus(); });
        connect(controller, &VpController::documentReplaced, this, &VpCanvas::resetView);
        connect(controller, &QObject::destroyed, this, [this] {
            m_controller.clear();
            resetView();
            emit controllerChanged();
        });
    }
    resetView();
    emit controllerChanged();
}

void VpCanvas::resetView()
{
    setSpaceHeld(false);
    m_panning = false;
    m_toolPointerActive = false;
    m_navigationPointerActive = false;
    m_cursorOnCanvas = false;
    m_cursorPoint = QPointF();
    m_scale = 1;
    m_offset = QPointF();
    m_fitPending = true;
    m_fillView = false;
    updateNavigationCursor();
    updateAuxiliaryOverlay();
    updateContentLayers();
    fitView();
    emit viewChanged();
    update();
}

void VpCanvas::paint(QPainter *painter)
{
    if (!m_controller || !document().hasLoadedImage())
        return;
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter->translate(m_offset);
    painter->scale(m_scale, m_scale);
    painter->drawImage(QPointF(), document().background());
    painter->restore();
}

// 仿制源用绿色十字标出，线宽与臂长都按视图缩放换算，屏幕上尺寸恒定
void VpCanvas::drawCloneMarker(QPainter *painter)
{
    if (!m_controller->hasCloneSource())
        return;
    const QPointF marker = m_controller->cloneMarker();
    const qreal arm = 7.0 / m_scale;
    painter->save();
    painter->setPen(QPen(QColor("#00e676"), 1.0 / m_scale));
    painter->drawLine(QPointF(marker.x() - arm, marker.y()), QPointF(marker.x() + arm, marker.y()));
    painter->drawLine(QPointF(marker.x(), marker.y() - arm), QPointF(marker.x(), marker.y() + arm));
    painter->restore();
}

void VpCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (m_auxiliaryOverlay)
        m_auxiliaryOverlay->setSize(newGeometry.size());
    if (m_paintLayerItem) m_paintLayerItem->setSize(newGeometry.size());
    if (newGeometry.size() == oldGeometry.size())
        return;
    if (m_fitPending) {
        // 等待初始布局完成再适配一次，避免宽、高分别变化时连续缩放。
        polish();
    } else {
        // 窗口缩放只改变可视区域，保持比例和视图中心所对应的图像位置。
        m_offset += QPointF((newGeometry.width() - oldGeometry.width()) / 2,
                            (newGeometry.height() - oldGeometry.height()) / 2);
        updateViewTransform();
    }
}

void VpCanvas::updatePolish()
{
    if (!m_fitPending || !m_controller || document().background().isNull()
        || width() <= 0 || height() <= 0)
        return;
    updateViewTransform();
    m_fitPending = false;
}

void VpCanvas::updateCursorPoint(const QPointF &widgetPoint)
{
    if (!m_controller)
        return;
    m_cursorOnCanvas = true;
    m_cursorPoint = widgetToImage(widgetPoint);
    if (m_controller->tool() != Tool::Zoom && m_controller->tool() != Tool::Eyedropper)
        updateNavigationCursor();
    if (m_controller->tool() == Tool::CreatePlane) {
        updateAuxiliaryOverlay();
        return;
    }
    if (m_controller->tool() == Tool::CloneStamp)
        m_controller->hoverClone(m_cursorPoint);
    if (m_controller->tool() == Tool::Brush || m_controller->tool() == Tool::CloneStamp
        || m_controller->tool() == Tool::Marquee)
        updateAuxiliaryOverlay();
    if (m_controller->cursorPreviewVisible())
        updateCursorPreview();
}

void VpCanvas::hoverEnterEvent(QHoverEvent *event)
{
    if (m_controller && document().hasLoadedImage())
        forceActiveFocus();
    updateCursorPoint(event->position());
    updateNavigationCursor(event->modifiers() & Qt::AltModifier);
    QQuickPaintedItem::hoverEnterEvent(event);
}

void VpCanvas::hoverMoveEvent(QHoverEvent *event)
{
    updateCursorPoint(event->position());
    QQuickPaintedItem::hoverMoveEvent(event);
}

void VpCanvas::hoverLeaveEvent(QHoverEvent *event)
{
    m_cursorOnCanvas = false;
    m_cursorPoint = QPointF();
    if (m_controller && (m_controller->tool() == Tool::Brush || m_controller->tool() == Tool::CloneStamp
        || m_controller->tool() == Tool::Marquee))
        updateAuxiliaryOverlay();
    if (m_controller && m_controller->tool() == Tool::CreatePlane) {
        updateAuxiliaryOverlay();
        QQuickPaintedItem::hoverLeaveEvent(event);
        return;
    }
    if (m_controller && m_controller->cursorPreviewVisible())
        updateCursorPreview();
    QQuickPaintedItem::hoverLeaveEvent(event);
}

void VpCanvas::keyPressEvent(QKeyEvent *event)
{
    if (!m_controller) {
        QQuickPaintedItem::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat() && document().hasLoadedImage()) setSpaceHeld(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Control)
        updateAuxiliaryOverlay();
    if (event->key() == Qt::Key_Alt) {
        updateNavigationCursor(true);
        event->accept();
        return;
    }
    const int key = event->key();
    if (key != Qt::Key_Backspace && key != Qt::Key_Delete) {
        QQuickPaintedItem::keyPressEvent(event);
        return;
    }
    m_controller->deleteSelection();
    if (m_controller->tool() == Tool::CreatePlane || m_controller->tool() == Tool::EditPlane)
        updateAuxiliaryOverlay();
    else
        updateBaseContent();
    event->accept();
}

QPointF VpCanvas::widgetToImage(const QPointF &widgetPoint) const
{
    return (widgetPoint - m_offset) / m_scale;
}

void VpCanvas::updateAuxiliaryOverlay()
{
    if (!m_auxiliaryOverlay)
        return;
    const bool loaded = m_controller && document().hasLoadedImage();
    const bool creating = loaded && m_controller->tool() == Tool::CreatePlane;
    const int highlighted = loaded && !temporaryHandActive() && !m_panning
        && (m_cursorOnCanvas || m_controller->brushDrawing() || m_controller->cloneDrawing())
        ? m_controller->paintTargetPlane(m_cursorPoint) : -1;
    m_auxiliaryOverlay->setVisible(loaded && (creating || !document().planes().isEmpty()));
    m_auxiliaryOverlay->setPlaneGuides(loaded ? document().planes() : QVector<PerspectivePlane>(),
        loaded ? document().selectedPlane() : -1, loaded && m_controller->tool() == Tool::EditPlane,
        QGuiApplication::keyboardModifiers() & Qt::ControlModifier,
        loaded ? m_controller->extrudePreview() : nullptr, loaded ? m_controller->gridSize() : 50,
        m_scale, m_offset, loaded ? document().background().size() : QSize(),
        loaded ? m_controller->draggedPlaneControlPoint() : -1, highlighted);
    m_auxiliaryOverlay->setCreationPreview(creating ? m_controller->creationPoints() : QVector<QPointF>(),
        m_cursorPoint, m_cursorOnCanvas, m_scale, m_offset, loaded ? document().background().size() : QSize());
    QPainterPath marquee, floating;
    QVector<QPointF> handles;
    if (loaded) {
        marquee = m_controller->selectionOutline();
        const int index = document().selectedFloatingSelection();
        if (index >= 0 && index < document().floatingSelections().size()) {
            const auto projection = FloatingSelectionProjection::forSelection(document().floatingSelection(index));
            floating = projection->canvasOutline();
            if (m_controller->tool() == Tool::Transform)
                handles = projection->transformHandles();
        }
    }
    m_auxiliaryOverlay->setSelectionGuides(marquee, floating, handles, m_scale, m_offset);
    m_auxiliaryOverlay->setVisible(loaded && (creating || !document().planes().isEmpty()
        || !floating.isEmpty() || !marquee.isEmpty()));
    if (m_controller && m_controller->tool() != Tool::Zoom && m_controller->tool() != Tool::Eyedropper)
        updateNavigationCursor();
}

void VpCanvas::updateViewTransform()
{
    if (!m_controller)
        return;
    const QImage &background = document().background();
    if (background.isNull() || width() <= 0 || height() <= 0)
        return;
    if (m_fitPending) {
        const qreal sx = qMax(1.0, width() - ViewMargin * 2) / background.width();
        const qreal sy = qMax(1.0, height() - ViewMargin * 2) / background.height();
        m_scale = qBound(0.063, m_fillView ? qMax(sx, sy) : qMin(sx, sy), 16.0);
        m_offset = QPointF((width() - background.width() * m_scale) / 2,
                           (height() - background.height() * m_scale) / 2);
    }
    const QSizeF shown = QSizeF(background.size()) * m_scale;
    m_offset.setX(shown.width() <= width() ? (width() - shown.width()) / 2
                                         : qBound(width() - shown.width(), m_offset.x(), 0.0));
    m_offset.setY(shown.height() <= height() ? (height() - shown.height()) / 2
                                           : qBound(height() - shown.height(), m_offset.y(), 0.0));
    emit viewChanged();
    updateAuxiliaryOverlay();
    updateContentLayers();
    m_paintLayerItem->update();
    update();
}

void VpCanvas::updateContentLayers()
{
    if (!m_selectionPreview) return;
    const bool loaded = m_controller && document().hasLoadedImage();
    const int count = loaded ? document().floatingSelections().size() : 0;
    while (m_floatingItems.size() > count) delete m_floatingItems.takeLast();
    while (m_floatingItems.size() < count) m_floatingItems.append(new FloatingContentItem(this));
    for (int i = 0; i < count; ++i) {
        m_floatingItems[i]->setZ(.5 + .1 * i / qMax(1, count));
        m_floatingItems[i]->setImages({document().floatingSelection(i)}, m_scale, m_offset, size(), document().background().size());
    }
    QVector<FloatingSelection> preview;
    if (loaded && !m_controller->selectionPreview().bitmap.isNull())
        preview.append(m_controller->selectionPreview());
    m_selectionPreview->setImages(preview, m_scale, m_offset, size(),
        loaded ? document().background().size() : QSize());
    m_paintLayerItem->setVisible(loaded);
    updateCursorPreview();
    if (loaded) m_controller->setSelectionPreviewDeviceScale(m_scale * (window() ? window()->devicePixelRatio() : 1));
}

void VpCanvas::updateBaseContent()
{
    const qint64 backgroundKey = m_controller ? document().background().cacheKey() : 0;
    const qint64 paintKey = m_controller ? document().paintLayer().cacheKey() : 0;
    if (m_backgroundKey != backgroundKey) {
        m_backgroundKey = backgroundKey;
        update();
    }
    if (m_paintKey != paintKey) {
        m_paintKey = paintKey;
        m_paintLayerItem->update();
    }
}

void VpCanvas::updatePaintRegion(const QRect &dirty)
{
    if (!m_controller || dirty.isEmpty()) return;
    m_paintKey = document().paintLayer().cacheKey();
    const QRectF mapped(m_offset + QPointF(dirty.topLeft()) * m_scale, QSizeF(dirty.size()) * m_scale);
    const QRect region = mapped.adjusted(-2,-2,2,2).intersected(boundingRect()).toAlignedRect();
    // An empty update rectangle means a FULL update in QQuickPaintedItem.
    if (!region.isEmpty()) m_paintLayerItem->update(region);
    updateCursorPreview();
}

void VpCanvas::updateCursorPreview()
{
    if (!m_cursorPreview) return;
    const bool loaded = m_controller && document().hasLoadedImage();
    const bool brush = loaded && !temporaryHandActive() && m_controller->tool() == Tool::Brush && m_cursorOnCanvas;
    const bool clone = loaded && !temporaryHandActive() && m_controller->tool() == Tool::CloneStamp && m_controller->hasCloneSource();
    QRectF bounds;
    if (brush || (clone && m_cursorOnCanvas)) {
        const QRectF canvasBounds = brush ? m_controller->brushPreviewBounds(m_cursorPoint)
                                         : m_controller->clonePreviewBounds(m_cursorPoint);
        if (!canvasBounds.isEmpty())
            bounds = QRectF(m_offset + canvasBounds.topLeft() * m_scale, canvasBounds.size() * m_scale)
                         .adjusted(-2,-2,2,2).intersected(boundingRect());
    }
    const QRect clipped = bounds.toAlignedRect();
    m_cursorPreview->setPosition(clipped.topLeft()); m_cursorPreview->setSize(clipped.size());
    m_cursorPreview->setVisible(!clipped.isEmpty());
    if (!clipped.isEmpty()) m_cursorPreview->update();
    QRect markerBounds;
    if (clone) {
        const QPointF marker = m_offset + m_controller->cloneMarker() * m_scale;
        markerBounds = QRectF(marker - QPointF(9,9), QSizeF(18,18)).intersected(boundingRect()).toAlignedRect();
    }
    m_cloneMarkerItem->setPosition(markerBounds.topLeft()); m_cloneMarkerItem->setSize(markerBounds.size());
    m_cloneMarkerItem->setVisible(!markerBounds.isEmpty());
    if (!markerBounds.isEmpty()) m_cloneMarkerItem->update();
}

qreal VpCanvas::horizontalSize() const
{
    if (!m_controller)
        return 1;
    return qMin(1.0, width() / qMax(1.0, document().background().width() * m_scale));
}

qreal VpCanvas::verticalSize() const
{
    if (!m_controller)
        return 1;
    return qMin(1.0, height() / qMax(1.0, document().background().height() * m_scale));
}

qreal VpCanvas::horizontalPosition() const
{
    return horizontalSize() >= 1 ? 0 : -m_offset.x() / (document().background().width() * m_scale);
}

qreal VpCanvas::verticalPosition() const
{
    return verticalSize() >= 1 ? 0 : -m_offset.y() / (document().background().height() * m_scale);
}

void VpCanvas::scrollTo(qreal horizontal, qreal vertical)
{
    if (!m_controller)
        return;
    m_fitPending = false;
    m_offset = QPointF(-horizontal * document().background().width() * m_scale,
                      -vertical * document().background().height() * m_scale);
    updateViewTransform();
}

void VpCanvas::zoomAt(qreal scale, const QPointF &anchor)
{
    if (!m_controller || !std::isfinite(scale) || document().background().isNull())
        return;
    const QPointF point = widgetToImage(anchor);
    m_fitPending = false;
    m_scale = qBound(0.063, scale, 16.0);
    m_offset = anchor - point * m_scale;
    updateViewTransform();
}

void VpCanvas::setZoom(qreal scale)
{
    zoomAt(scale, QPointF(width() / 2, height() / 2));
}

void VpCanvas::stepAt(bool out, const QPointF &anchor)
{
    const auto &levels = ZoomLevels;
    int nearest = 0;
    for (int i = 1; i < int(levels.size()); ++i)
        if (std::abs(levels[i] - m_scale) < std::abs(levels[nearest] - m_scale))
            nearest = i;
    zoomAt(levels[qBound(0, nearest + (out ? -1 : 1), int(levels.size()) - 1)], anchor);
}

void VpCanvas::zoomStep(bool out)
{
    stepAt(out, QPointF(width() / 2, height() / 2));
}

void VpCanvas::fitView(bool fill)
{
    m_fitPending = true;
    m_fillView = fill;
    updatePolish();
    if (m_fitPending)
        polish();
}

void VpCanvas::setEyedropperCursorSource(const QUrl &source)
{
    if (m_eyedropperCursorSource == source) return;
    m_eyedropperCursorSource = source;
    emit eyedropperCursorSourceChanged();
    if (m_controller && m_controller->tool() == Tool::Eyedropper)
        updateNavigationCursor();
}

void VpCanvas::setZoomCursorDirectory(const QUrl &directory)
{
    if (m_zoomCursorDirectory == directory) return;
    m_zoomCursorDirectory = directory;
    emit zoomCursorDirectoryChanged();
    if (m_controller)
        updateNavigationCursor(QGuiApplication::keyboardModifiers() & Qt::AltModifier);
}

bool VpCanvas::setResourceCursor(const QUrl &url, const QPointF &relativeHotspot, qreal rotation)
{
    if (url.isEmpty()) return false;
    const QString path = url.isLocalFile() ? url.toLocalFile()
        : url.scheme() == QStringLiteral("qrc") ? QStringLiteral(":") + url.path() : url.toString();
    const QScreen *screen = window() ? window()->screen() : QGuiApplication::primaryScreen();
    const qreal ratio = screen ? screen->logicalDotsPerInchX() / 96.0 : 1;
    const QString key = path + ':' + QString::number(ratio, 'g', 12) + ':' + QString::number(rotation)
        + ':' + QString::number(relativeHotspot.x()) + ':' + QString::number(relativeHotspot.y());
    auto cached = m_interactionCursors.constFind(key);
    if (cached == m_interactionCursors.constEnd()) {
        QImageReader reader(path);
        // Rasterize SVGs above cursor resolution before measuring their visible bounds.
        const QSize nativeSize = reader.size();
        if (!nativeSize.isValid()) return false;
        reader.setScaledSize(nativeSize.scaled(256, 256, Qt::KeepAspectRatio));
        QImage image = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return false;
        QPointF hotspot(qMin(image.width() - 1.0, image.width() * relativeHotspot.x()),
                        qMin(image.height() - 1.0, image.height() * relativeHotspot.y()));
        if (rotation != 0) {
            const QTransform transform = QTransform().rotate(rotation);
            hotspot = QImage::trueMatrix(transform, image.width(), image.height()).map(hotspot);
            image = image.transformed(transform, Qt::SmoothTransformation);
        }
        int left = image.width(), top = image.height(), right = -1, bottom = -1;
        for (int y = 0; y < image.height(); ++y) {
            const auto *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                if (!qAlpha(pixels[x])) continue;
                left = qMin(left,x); top = qMin(top,y);
                right = qMax(right,x); bottom = qMax(bottom,y);
            }
        }
        if (right < left || bottom < top) return false;
        const QRect bounds(QPoint(left,top), QPoint(right,bottom));
        // Resize arrows should have the same length along their own axis.
        // Equal bounding-box widths make diagonal arrows visibly longer.
        const bool diagonal = path.endsWith(QStringLiteral("/cut_oblique.svg"))
            || path.endsWith(QStringLiteral("/cut_oblique_back.svg"));
        qreal axisExtent = qMax(bounds.width(), bounds.height());
        if (diagonal) {
            int minSum = image.width() + image.height(), maxSum = -minSum;
            int minDifference = minSum, maxDifference = -minSum;
            for (int y = top; y <= bottom; ++y) {
                const auto *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = left; x <= right; ++x) {
                    if (!qAlpha(pixels[x])) continue;
                    minSum = qMin(minSum, x + y); maxSum = qMax(maxSum, x + y);
                    minDifference = qMin(minDifference, x - y); maxDifference = qMax(maxDifference, x - y);
                }
            }
            axisExtent = qMax(maxSum - minSum, maxDifference - minDifference) / std::sqrt(2.0) + 1;
        }
        const int visibleSize = qMax(1, qRound(24 * ratio
            * qMax(bounds.width(), bounds.height()) / axisExtent));
        const QImage visible = image.copy(bounds).scaled(visibleSize, visibleSize,
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int padding = qMax(1, qRound(ratio));
        QPixmap pixmap(visible.width() + 2 * padding, visible.height() + 2 * padding);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.drawImage(padding, padding, visible);
        painter.end();
        hotspot -= bounds.topLeft();
        hotspot = QPointF(hotspot.x() * visible.width() / bounds.width(),
                          hotspot.y() * visible.height() / bounds.height()) + QPointF(padding,padding);
        m_interactionCursors.insert(key, QCursor(pixmap,
            qBound(0,qRound(hotspot.x()),pixmap.width()-1),
            qBound(0,qRound(hotspot.y()),pixmap.height()-1)));
        cached = m_interactionCursors.constFind(key);
    }
    setCursor(cached.value());
    return true;
}

void VpCanvas::updateNavigationCursor(bool alt)
{
    if (!m_controller) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (temporaryHandActive() || m_panning) {
        setCursor(m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    } else if (m_controller->tool() == Tool::Eyedropper) {
        if (!setResourceCursor(m_eyedropperCursorSource, QPointF(0,1)))
            setCursor(Qt::CrossCursor);
    } else if (m_controller->tool() == Tool::Hand) {
        setCursor(m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    } else if (m_controller->tool() == Tool::Zoom) {
        const QUrl url = m_zoomCursorDirectory.resolved(QUrl(alt
            ? QStringLiteral("image_zoomout.svg") : QStringLiteral("image_zoomin.svg")));
        // Preserve the artwork's magnifier center at (15.5, 15.5) in its 34 px canvas.
        if (!setResourceCursor(url, QPointF(15.5/34,15.5/34)))
            setCursor(Qt::ArrowCursor);
    } else if (!m_toolPointerActive) {
        // Keep the initial interaction cursor until release, even outside its hit region.
        const QString resource = m_cursorOnCanvas
            ? m_controller->interactionCursorResource(m_cursorPoint, m_scale) : QString();
        const qreal rotation = resource == QStringLiteral("image_cursor_rotate.svg")
            ? m_controller->interactionCursorRotation(m_cursorPoint, m_scale) : 0;
        const QPointF hotspot = resource == QStringLiteral("image_choice.svg") ? QPointF(0,0) : QPointF(.5,.5);
        if (resource.isEmpty() || !setResourceCursor(m_zoomCursorDirectory.resolved(QUrl(resource)),
                                                     hotspot, rotation))
            setCursor(Qt::ArrowCursor);
    }
}

void VpCanvas::setSpaceHeld(bool held)
{
    if (m_spaceHeld == held) return;
    const bool wasActive = temporaryHandActive();
    m_spaceHeld = held;
    if (!held && m_navigationPointerActive && m_controller && m_controller->tool() != Tool::Hand)
        m_panning = false;
    if (wasActive != temporaryHandActive()) emit temporaryHandActiveChanged();
    updateNavigationCursor(QGuiApplication::keyboardModifiers() & Qt::AltModifier);
    updateAuxiliaryOverlay();
    updateCursorPreview();
}

void VpCanvas::focusOutEvent(QFocusEvent *event)
{
    m_panning = false;
    setSpaceHeld(false);
    updateNavigationCursor();
    QQuickPaintedItem::focusOutEvent(event);
}

void VpCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) setSpaceHeld(false);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Control)
        updateAuxiliaryOverlay();
    if (event->key() == Qt::Key_Alt) {
        updateNavigationCursor(false);
        event->accept();
        return;
    }
    QQuickPaintedItem::keyReleaseEvent(event);
}

void VpCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (!m_controller) {
        event->ignore();
        return;
    }
    forceActiveFocus();
    updateCursorPoint(event->position());
    event->accept();
    if (temporaryHandActive() || m_controller->tool() == Tool::Hand) {
        m_navigationPointerActive = true;
        m_fitPending = false;
        m_panning = true;
        m_panPoint = event->position();
        updateNavigationCursor();
    } else if (m_controller->tool() == Tool::Zoom) {
        m_navigationPointerActive = true;
        stepAt(event->modifiers() & Qt::AltModifier, event->position());
    } else {
        m_toolPointerActive = true;
        m_controller->pointerPress(widgetToImage(event->position()), m_scale, event->modifiers());
    }
}

void VpCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_controller) {
        event->ignore();
        return;
    }
    event->accept();
    if (m_panning) {
        if (!(event->buttons() & Qt::LeftButton)) {
            m_panning = false;
            updateNavigationCursor();
            return;
        }
        m_offset += event->position() - m_panPoint;
        m_panPoint = event->position();
        updateViewTransform();
    } else if (!m_navigationPointerActive) {
        updateCursorPoint(event->position());
        if (event->buttons() & Qt::LeftButton)
            m_controller->pointerMove(m_cursorPoint, event->modifiers());
    }
}

void VpCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (!m_controller) {
        event->ignore();
        return;
    }
    event->accept();
    const bool temporaryHandWasActive = temporaryHandActive();
    if (m_panning) {
        m_offset += event->position() - m_panPoint;
        m_panning = false;
        updateViewTransform();
        updateNavigationCursor();
    } else if (!m_navigationPointerActive) {
        m_controller->pointerRelease(widgetToImage(event->position()), event->modifiers());
    }
    m_navigationPointerActive = false;
    m_toolPointerActive = false;
    m_cursorPoint = widgetToImage(event->position());
    updateAuxiliaryOverlay();
    if (temporaryHandWasActive != temporaryHandActive()) emit temporaryHandActiveChanged();
    updateNavigationCursor(event->modifiers() & Qt::AltModifier);
    updateCursorPreview();
}

void VpCanvas::undo() { m_panning = false; if (m_controller) m_controller->undo(); }
void VpCanvas::redo() { m_panning = false; if (m_controller) m_controller->redo(); }

QVariantList VpCanvas::zoomLevels() const
{
    QVariantList levels;
    for (qreal scale : ZoomLevels)
        levels.append(scale);
    return levels;
}
