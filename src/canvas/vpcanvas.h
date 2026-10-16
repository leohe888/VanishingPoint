#pragma once

#include "vpcontroller.h"
#include "../core/tooltypes.h"

#include <QColor>
#include <QCursor>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QVariantList>
#include <QUrl>
#include <QQuickPaintedItem>

class AuxiliaryOverlayItem;
class FloatingContentItem;
class CursorPreviewItem;
class PaintLayerItem;

class VpCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(VpController *controller READ controller WRITE setController NOTIFY controllerChanged REQUIRED)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(bool temporaryHandActive READ temporaryHandActive NOTIFY temporaryHandActiveChanged)
    Q_PROPERTY(QUrl eyedropperCursorSource READ eyedropperCursorSource WRITE setEyedropperCursorSource NOTIFY eyedropperCursorSourceChanged)
    Q_PROPERTY(QUrl zoomCursorDirectory READ zoomCursorDirectory WRITE setZoomCursorDirectory NOTIFY zoomCursorDirectoryChanged)
    Q_PROPERTY(QVariantList zoomLevels READ zoomLevels CONSTANT)
    Q_PROPERTY(qreal horizontalSize READ horizontalSize NOTIFY viewChanged)
    Q_PROPERTY(qreal verticalSize READ verticalSize NOTIFY viewChanged)
    Q_PROPERTY(qreal horizontalPosition READ horizontalPosition NOTIFY viewChanged)
    Q_PROPERTY(qreal verticalPosition READ verticalPosition NOTIFY viewChanged)

public:
    using Tool = VpTools::Tool;

    explicit VpCanvas(QQuickItem *parent = nullptr);

    VpController *controller() const { return m_controller.data(); }
    void setController(VpController *controller);

    void paint(QPainter *painter) override;
    QVariantList zoomLevels() const;
    qreal zoom() const { return m_scale; }
    bool temporaryHandActive() const { return m_spaceHeld && !m_toolPointerActive; }
    QUrl eyedropperCursorSource() const { return m_eyedropperCursorSource; }
    void setEyedropperCursorSource(const QUrl &source);
    QUrl zoomCursorDirectory() const { return m_zoomCursorDirectory; }
    void setZoomCursorDirectory(const QUrl &directory);
    qreal horizontalSize() const;
    qreal verticalSize() const;
    qreal horizontalPosition() const;
    qreal verticalPosition() const;
    Q_INVOKABLE void setZoom(qreal scale);
    Q_INVOKABLE void zoomStep(bool out);
    Q_INVOKABLE void fitView(bool fill = false);
    Q_INVOKABLE void scrollTo(qreal horizontal, qreal vertical);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

signals:
    void controllerChanged();
    void temporaryHandActiveChanged();
    void viewChanged();
    void eyedropperCursorSourceChanged();
    void zoomCursorDirectoryChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void updatePolish() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void resetView();
    void setSpaceHeld(bool held);
    QPointF widgetToImage(const QPointF &widgetPoint) const;
    void updateViewTransform();
    void zoomAt(qreal scale, const QPointF &anchor);
    void stepAt(bool out, const QPointF &anchor);
    void updateNavigationCursor(bool alt = false);
    bool setResourceCursor(const QUrl &url, const QPointF &relativeHotspot, qreal rotation = 0);
    void updateCursorPoint(const QPointF &widgetPoint);
    void updateAuxiliaryOverlay();
    void updateContentLayers();
    void updateBaseContent();
    void updatePaintRegion(const QRect &dirty);
    void updateCursorPreview();
    const VpDocument &document() const { return m_controller->document(); }
    void drawCloneMarker(QPainter *painter);





    QPointer<VpController> m_controller;
    QMetaObject::Connection m_documentConnection;
    AuxiliaryOverlayItem *m_auxiliaryOverlay = nullptr;
    FloatingContentItem *m_selectionPreview = nullptr;
    QVector<FloatingContentItem *> m_floatingItems;
    qint64 m_backgroundKey = 0;
    qint64 m_paintKey = 0;
    CursorPreviewItem *m_cursorPreview = nullptr;
    CursorPreviewItem *m_cloneMarkerItem = nullptr;
    PaintLayerItem *m_paintLayerItem = nullptr;
    QPointF m_cursorPoint;
    bool m_cursorOnCanvas = false;
    QUrl m_eyedropperCursorSource;
    QUrl m_zoomCursorDirectory;
    QHash<QString, QCursor> m_interactionCursors;

    qreal m_scale = 1.0;
    QPointF m_offset;
    bool m_fitPending = true;
    bool m_fillView = false;
    bool m_panning = false;
    bool m_spaceHeld = false;
    bool m_toolPointerActive = false;
    bool m_navigationPointerActive = false;
    QPointF m_panPoint;
};
