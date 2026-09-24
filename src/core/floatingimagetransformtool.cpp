#include "floatingimagetransformtool.h"

#include "floatingimageprojection.h"

#include <QLineF>
#include <QtMath>

#include <cmath>

namespace {

FloatingImage scaledImage(const FloatingImage &startImage, int handleIndex,
                          const QPointF &placementPoint, bool keepAspectRatio,
                          bool scaleFromCenter)
{
    FloatingImage result = startImage;
    if (handleIndex < 0 || handleIndex >= 8
        || !std::isfinite(placementPoint.x()) || !std::isfinite(placementPoint.y())) {
        return result;
    }

    const QRectF startRect(startImage.placementOrigin, startImage.displaySize());
    QTransform rotation;
    rotation.translate(startRect.center().x(), startRect.center().y());
    rotation.rotate(startImage.rotationDegrees);
    rotation.translate(-startRect.center().x(), -startRect.center().y());
    const QPointF unrotatedPoint = rotation.inverted().map(placementPoint);
    const bool left = handleIndex == 0 || handleIndex == 3 || handleIndex == 7;
    const bool right = handleIndex == 1 || handleIndex == 2 || handleIndex == 5;
    const bool top = handleIndex == 0 || handleIndex == 1 || handleIndex == 4;
    const bool bottom = handleIndex == 2 || handleIndex == 3 || handleIndex == 6;
    qreal width = startRect.width();
    qreal height = startRect.height();
    const QPointF anchor = scaleFromCenter
        ? startRect.center()
        : QPointF(left ? startRect.right() : startRect.left(),
                  top ? startRect.bottom() : startRect.top());
    const qreal multiplier = scaleFromCenter ? 2 : 1;
    if (left || right) {
        width = qBound(1.0,
                       (left ? anchor.x() - unrotatedPoint.x()
                             : unrotatedPoint.x() - anchor.x()) * multiplier,
                       100000.0);
    }
    if (top || bottom) {
        height = qBound(1.0,
                        (top ? anchor.y() - unrotatedPoint.y()
                             : unrotatedPoint.y() - anchor.y()) * multiplier,
                        100000.0);
    }
    if (keepAspectRatio) {
        qreal factor = (left || right) ? width / startRect.width() : height / startRect.height();
        if (handleIndex < 4)
            factor = qMax(factor, height / startRect.height());
        factor = qBound(qMax(1 / startRect.width(), 1 / startRect.height()), factor,
                        qMin(100000 / startRect.width(), 100000 / startRect.height()));
        width = startRect.width() * factor;
        height = startRect.height() * factor;
    }

    result.placementOrigin = QPointF(
        left ? startRect.right() - width
             : (right ? startRect.left() : startRect.center().x() - width / 2),
        top ? startRect.bottom() - height
            : (bottom ? startRect.top() : startRect.center().y() - height / 2));
    if (scaleFromCenter)
        result.placementOrigin = startRect.center() - QPointF(width / 2, height / 2);
    const QPointF halfSize(width / 2, height / 2);
    result.placementOrigin = rotation.map(result.placementOrigin + halfSize) - halfSize;
    result.scaleFactors = QPointF(width / startImage.bitmap.width(),
                                 height / startImage.bitmap.height());
    return result;
}

FloatingImage rotatedImage(const FloatingImage &startImage, const QPointF &pressPosition,
                           const QPointF &currentPosition, bool snapToIncrement)
{
    FloatingImage result = startImage;
    const QPointF center = QRectF(startImage.placementOrigin, startImage.displaySize()).center();
    const QPointF startVector = pressPosition - center;
    const QPointF currentVector = currentPosition - center;
    if (QLineF(center, pressPosition).length() < 1e-6
        || QLineF(center, currentPosition).length() < 1e-6) {
        return result;
    }

    qreal angle = startImage.rotationDegrees
        + qRadiansToDegrees(std::atan2(currentVector.y(), currentVector.x())
                            - std::atan2(startVector.y(), startVector.x()));
    if (!std::isfinite(angle))
        return result;
    if (snapToIncrement)
        angle = qRound(angle / 15) * 15;
    result.rotationDegrees = std::remainder(angle, 360.0);
    return result;
}

} // namespace

bool FloatingImageTransformTool::beginTransform(const FloatingImage &image,
                                                const QPointF &canvasPoint,
                                                int handleIndex, Mode mode)
{
    if (handleIndex < 0 || handleIndex >= 8
        || (mode != Mode::Scale && mode != Mode::Rotate)) {
        return false;
    }
    m_startImage = image;
    const QPointF handlePosition = image.transformHandlePositions()[handleIndex];
    image.mapPlacementToCanvas(handlePosition, &m_activeQuadIndex);
    if (!image.mapCanvasToPlacement(canvasPoint, &m_pressPosition, m_activeQuadIndex))
        return false;
    m_grabOffset = m_pressPosition - handlePosition;
    m_activeHandle = handleIndex;
    m_mode = mode;
    return true;
}

void FloatingImageTransformTool::beginMove(const FloatingImage &image,
                                           const QPointF &placementOffset)
{
    m_startImage = image;
    m_grabOffset = placementOffset;
    m_activeQuadIndex = -1;
    m_mode = Mode::Move;
}

bool FloatingImageTransformTool::update(const QPointF &canvasPoint, bool constrain,
                                        bool fromCenter, FloatingImage *result) const
{
    if (!result || m_mode == Mode::Idle)
        return false;
    QPointF placementPoint;
    if (!m_startImage.mapCanvasToPlacement(canvasPoint, &placementPoint, m_activeQuadIndex))
        return false;

    if (m_mode == Mode::Rotate) {
        *result = rotatedImage(m_startImage, m_pressPosition, placementPoint, constrain);
    } else if (m_mode == Mode::Scale) {
        *result = scaledImage(m_startImage, m_activeHandle, placementPoint - m_grabOffset,
                              constrain, fromCenter);
    } else {
        *result = m_startImage;
        result->placementOrigin = placementPoint - m_grabOffset;
    }
    return true;
}

int FloatingImageTransformTool::handleAt(const FloatingImage &image,
                                         const QPointF &canvasPoint, qreal viewScale)
{
    const auto projection = FloatingImageProjection::forImage(image);
    const auto &handles = projection->transformHandles();
    for (int index = 0; index < handles.size(); ++index) {
        if (QLineF(canvasPoint, handles[index]).length() <= 8 / viewScale)
            return index;
    }
    return -1;
}

int FloatingImageTransformTool::rotationCornerAt(const FloatingImage &image,
                                                 const QPointF &canvasPoint,
                                                 qreal viewScale)
{
    const auto projection = FloatingImageProjection::forImage(image);
    const auto &handles = projection->transformHandles();
    for (int index = 0; index < 4; ++index) {
        const qreal distance = QLineF(canvasPoint, handles[index]).length() * viewScale;
        if (distance > 8 && distance <= 24 && !projection->canvasOutline().contains(canvasPoint))
            return index;
    }
    return -1;
}
