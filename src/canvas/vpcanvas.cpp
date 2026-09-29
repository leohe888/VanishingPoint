#include "vpcanvas.h"

#include "core/floatingimageprojection.h"
#include "core/scenerenderer.h"

#include <QCursor>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QGuiApplication>
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

    // 选中浮动图像或存在选区时让虚线跑起来；都没有就什么都不做，避免空转重绘。
    auto *antsTimer = new QTimer(this);
    antsTimer->setInterval(80);
    connect(antsTimer, &QTimer::timeout, this, [this] {
        if (!m_controller || !isVisible()
            || (document().selectedFloatingImage() < 0 && m_controller->selectionRect().isEmpty()))
            return;
        m_antsPhase = (m_antsPhase + 1) % 8;
        update();
    });
    antsTimer->start();

    updateViewTransform();
}

void VpCanvas::setController(VpController *controller)
{
    if (m_controller == controller)
        return;
    if (m_controller)
        disconnect(m_controller.data(), nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(controller, &VpController::toolChanged, this, [this] {
            m_panning = false;
            updateNavigationCursor(QGuiApplication::keyboardModifiers() & Qt::AltModifier);
        });
        connect(controller, &VpController::repaintRequested, this, [this] { update(); });
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
    m_panning = false;
    m_cursorOnCanvas = false;
    m_cursorPoint = QPointF();
    m_scale = 1;
    m_offset = QPointF();
    m_fitView = true;
    m_fillView = false;
    updateNavigationCursor();
    updateViewTransform();
    emit viewChanged();
    update();
}

void VpCanvas::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), QColor("#4D4D4D"));
    if (!m_controller || !document().hasLoadedImage()) {
        painter->setPen(QColor("#dddddd"));
        painter->drawText(boundingRect(), Qt::AlignCenter, tr("请打开一张图片开始操作（Ctrl+O）"));
        return;
    }
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter->translate(m_offset);
    painter->scale(m_scale, m_scale);
    SceneRenderer renderer(document());
    renderer.renderContent(*painter, m_scale);
    SceneRenderer::Guides guides;
    guides.creationPoints = m_controller->creationPoints();
    guides.extrudePreview = m_controller->extrudePreview();
    guides.editHandlesVisible = m_controller->tool() == Tool::EditPlane;
    guides.extrudeHandles = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    guides.antsPhase = m_antsPhase;
    guides.gridSize = m_controller->gridSize();
    if (m_controller->tool() == Tool::CreatePlane)
        guides.cursorPoint = m_cursorPoint;
    renderer.renderGuides(*painter, m_scale, guides);
    // 光标离开画布时不画预览：空点 (0,0) 同时也是合法的图像坐标
    const bool cursorOnCanvas = m_cursorOnCanvas;
    if (cursorOnCanvas && m_controller->tool() == Tool::Brush)
        m_controller->renderBrushPreview(*painter, m_cursorPoint);
    if (m_controller->tool() == Tool::CloneStamp) {
        if (cursorOnCanvas)
            m_controller->renderClonePreview(*painter, m_cursorPoint);
        drawCloneMarker(painter);
    }
    drawFloatingImageHandles(painter);
    drawSelectionOutline(painter);
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
    if (newGeometry.size() != oldGeometry.size())
        updateViewTransform();
}

void VpCanvas::updateCursorPoint(const QPointF &widgetPoint)
{
    if (!m_controller)
        return;
    m_cursorOnCanvas = true;
    m_cursorPoint = widgetToImage(widgetPoint);
    if (m_controller->tool() == Tool::CloneStamp)
        m_controller->hoverClone(m_cursorPoint);
    if (m_controller->cursorPreviewVisible())
        update();
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
    if (m_controller && m_controller->cursorPreviewVisible())
        update();
    QQuickPaintedItem::hoverLeaveEvent(event);
}

void VpCanvas::keyPressEvent(QKeyEvent *event)
{
    if (!m_controller) {
        QQuickPaintedItem::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Control)
        update();
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
    update();
    event->accept();
}

QPointF VpCanvas::widgetToImage(const QPointF &widgetPoint) const
{
    return (widgetPoint - m_offset) / m_scale;
}

void VpCanvas::updateViewTransform()
{
    if (!m_controller)
        return;
    const QImage &background = document().background();
    if (background.isNull() || width() <= 0 || height() <= 0)
        return;
    if (m_fitView) {
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
    update();
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
    m_fitView = false;
    m_offset = QPointF(-horizontal * document().background().width() * m_scale,
                      -vertical * document().background().height() * m_scale);
    updateViewTransform();
}

void VpCanvas::zoomAt(qreal scale, const QPointF &anchor)
{
    if (!m_controller || !std::isfinite(scale) || document().background().isNull())
        return;
    const QPointF point = widgetToImage(anchor);
    m_fitView = false;
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
    m_fitView = true;
    m_fillView = fill;
    updateViewTransform();
}

void VpCanvas::updateNavigationCursor(bool alt)
{
    if (!m_controller) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (m_controller->tool() == Tool::Hand) {
        setCursor(m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    } else if (m_controller->tool() == Tool::Zoom) {
        static const auto makeCursor = [](bool out) {
        QPixmap icon(32, 32);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 3));
        painter.drawEllipse(QRectF(3, 3, 18, 18));
        painter.drawLine(19, 19, 28, 28);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawEllipse(QRectF(3, 3, 18, 18));
        painter.drawLine(19, 19, 28, 28);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawLine(7, 12, 17, 12);
        if (!out)
            painter.drawLine(12, 7, 12, 17);
        painter.end();
        return QCursor(icon, 12, 12);
        };
        static const std::array<QCursor, 2> cursors{makeCursor(false), makeCursor(true)};
        setCursor(cursors[alt ? 1 : 0]);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void VpCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control)
        update();
    if (event->key() == Qt::Key_Alt) {
        updateNavigationCursor(false);
        event->accept();
        return;
    }
    QQuickPaintedItem::keyReleaseEvent(event);
}

// 变换工具下画出浮动图像的 8 个控制点；尺寸与线宽按视图缩放换算，屏幕上恒定。
void VpCanvas::drawFloatingImageHandles(QPainter *painter)
{
    if (m_controller->tool() != Tool::Transform || document().selectedFloatingImage() < 0)
        return;
    const auto projection = FloatingImageProjection::forImage(
        document().floatingImage(document().selectedFloatingImage()));
    const qreal half = 4.0 / m_scale;
    painter->save();
    painter->setPen(QPen(QColor("#1769aa"), 1.0 / m_scale));
    painter->setBrush(Qt::white);
    for (const QPointF &control : projection->transformHandles())
        painter->drawRect(QRectF(control.x() - half, control.y() - half, half * 2, half * 2));
    painter->restore();
}

// 选区轮廓按控件坐标描边（先 resetTransform 再整体缩放），
// 白 1px 打底 + 黑虚线，线宽与虚线间距在屏幕上恒定。
void VpCanvas::drawSelectionOutline(QPainter *painter)
{
    if (m_controller->tool() != Tool::Marquee || m_controller->selectionRect().isEmpty())
        return;
    painter->save();
    painter->resetTransform();
    const QPainterPath outline =
        QTransform::fromTranslate(m_offset.x(), m_offset.y())
            .map(QTransform::fromScale(m_scale, m_scale).map(m_controller->selectionOutline()));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(Qt::white, 1));
    painter->drawPath(outline);
    QPen pen(Qt::black, 1);
    pen.setDashPattern({4, 4});
    pen.setDashOffset(m_antsPhase);
    painter->setPen(pen);
    painter->drawPath(outline);
    painter->restore();
}

void VpCanvas::mousePressEvent(QMouseEvent *event)
{
    if (!m_controller) {
        event->ignore();
        return;
    }
    forceActiveFocus();
    event->accept();
    if (m_controller->tool() == Tool::Hand) {
        m_fitView = false;
        m_panning = true;
        m_panPoint = event->position();
        updateNavigationCursor();
    } else if (m_controller->tool() == Tool::Zoom) {
        stepAt(event->modifiers() & Qt::AltModifier, event->position());
    } else {
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
        m_offset += event->position() - m_panPoint;
        m_panPoint = event->position();
        updateViewTransform();
    } else {
        updateCursorPoint(event->position());
        if (event->buttons() & Qt::LeftButton)
            m_controller->pointerMove(m_cursorPoint, event->modifiers());
    }
}

void VpCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_controller) {
        event->ignore();
        return;
    }
    event->accept();
    if (m_panning) {
        m_offset += event->position() - m_panPoint;
        m_panning = false;
        updateViewTransform();
        updateNavigationCursor();
    } else {
        m_controller->pointerRelease(widgetToImage(event->position()), event->modifiers());
    }
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
