#include "floatingselection.h"

#include <QtMath>
#include <QPainter>

#include <cmath>

QImage FloatingSelection::copyPixels(const QImage &source, const QImage &selectionMask)
{
    if (source.isNull() || selectionMask.isNull())
        return {};
    QImage mask = selectionMask.convertToFormat(QImage::Format_Grayscale8);
    if (mask.size() != source.size())
        mask = mask.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage result = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage alphaMask(source.size(), QImage::Format_ARGB32_Premultiplied);
    if (mask.isNull() || result.isNull() || alphaMask.isNull())
        return {};
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *coverage = mask.constScanLine(y);
        QRgb *pixels = reinterpret_cast<QRgb *>(alphaMask.scanLine(y));
        for (int x = 0; x < mask.width(); ++x)
            pixels[x] = qRgba(0, 0, 0, coverage[x]);
    }
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, alphaMask);
    painter.end();
    int left = result.width(), top = result.height(), right = -1, bottom = -1;
    for (int y = 0; y < result.height(); ++y) {
        const QRgb *pixels = reinterpret_cast<const QRgb *>(result.constScanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            if (qAlpha(pixels[x]) == 0)
                continue;
            left = qMin(left, x); top = qMin(top, y);
            right = qMax(right, x); bottom = qMax(bottom, y);
        }
    }
    return right < left ? QImage() : result.copy(QRect(QPoint(left, top), QPoint(right, bottom)));
}

void FloatingSelection::setFeather(int value)
{
    value = qBound(0, value, 50);
    if (unfeatheredBitmap.isNull())
        unfeatheredBitmap = unhealedBitmap.isNull() ? bitmap : unhealedBitmap;
    bitmap = unfeatheredBitmap.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    feather = value;
    if (bitmap.isNull()) return;
    const QRectF rect = bitmapSelectionRect();
    const qreal denominator = std::sqrt(2.0) * value / 2.0;
    const auto coverage = [denominator](qreal p, qreal start, qreal end) {
        if (denominator == 0) return p >= start && p < end ? 1.0 : 0.0;
        return .5 * (std::erf((p - start) / denominator) - std::erf((p - end) / denominator));
    };
    QVector<qreal> horizontal(bitmap.width());
    for (int x = 0; x < bitmap.width(); ++x) horizontal[x] = coverage(x + .5, rect.left(), rect.right());
    for (int y = 0; y < bitmap.height(); ++y) {
        const qreal vertical = coverage(y + .5, rect.top(), rect.bottom());
        auto *row = reinterpret_cast<QRgb *>(bitmap.scanLine(y));
        for (int x = 0; x < bitmap.width(); ++x) {
            const qreal mask = horizontal[x] * vertical;
            row[x] = qRgba(qRound(qRed(row[x]) * mask), qRound(qGreen(row[x]) * mask),
                          qRound(qBlue(row[x]) * mask), qRound(qAlpha(row[x]) * mask));
        }
    }
    unhealedBitmap = healMode ? bitmap : QImage();
}

QSizeF FloatingSelection::displaySize() const
{
    const QRectF rect = bitmapSelectionRect();
    return QSizeF(rect.width() * scaleFactors.x(), rect.height() * scaleFactors.y());
}

QRectF FloatingSelection::bitmapSelectionRect() const
{
    return selectionRectInBitmap.isEmpty() ? QRectF(QPointF(), QSizeF(bitmap.size())) : selectionRectInBitmap;
}

QRectF FloatingSelection::placementRect() const
{
    const QPointF offset = bitmapSelectionRect().topLeft();
    return QRectF(placementOrigin + QPointF(offset.x() * scaleFactors.x(), offset.y() * scaleFactors.y()),
                  displaySize());
}

QTransform FloatingSelection::bitmapToPlacementTransform() const
{
    const QPointF center = placementRect().center();
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.rotate(rotationDegrees);
    transform.translate(placementOrigin.x() - center.x(), placementOrigin.y() - center.y());
    transform.scale(scaleFactors.x(), scaleFactors.y());
    return transform;
}

QVector<QPointF> FloatingSelection::transformHandlePositions() const
{
    const QRectF rect = bitmapSelectionRect();
    QVector<QPointF> positions{
        rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft(),
        (rect.topLeft() + rect.topRight()) / 2,
        (rect.topRight() + rect.bottomRight()) / 2,
        (rect.bottomLeft() + rect.bottomRight()) / 2,
        (rect.topLeft() + rect.bottomLeft()) / 2
    };
    const QTransform transform = bitmapToPlacementTransform();
    for (QPointF &position : positions)
        position = transform.map(position);
    return positions;
}

QPointF FloatingSelection::mapPlacementToCanvas(const QPointF &point, int *quadIndex) const
{
    int selectedQuad = hostQuadIndex;
    if (surfaceAttached) {
        for (int index = surfaceQuads.size() - 1; index >= 0; --index) {
            if (surfaceQuads[index].surfacePolygon().containsPoint(point, Qt::OddEvenFill)) {
                selectedQuad = index;
                break;
            }
        }
    }
    if (quadIndex)
        *quadIndex = selectedQuad;
    if (!surfaceAttached || selectedQuad < 0 || selectedQuad >= surfaceQuads.size())
        return point;

    QPointF canvasPoint;
    if (!surfaceQuads[selectedQuad].surfaceToCanvasTransform().mapForward(point, &canvasPoint))
        return QPointF(qQNaN(), qQNaN());
    return canvasPoint;
}

bool FloatingSelection::mapCanvasToPlacement(const QPointF &point, QPointF *result,
                                         int fallbackQuadIndex) const
{
    if (!result)
        return false;
    if (!surfaceAttached || surfaceQuads.isEmpty()) {
        *result = point;
        return true;
    }

    int selectedQuad = fallbackQuadIndex >= 0 ? fallbackQuadIndex : hostQuadIndex;
    for (int index = surfaceQuads.size() - 1; index >= 0; --index) {
        if (surfaceQuads[index].containsCanvasPoint(point)) {
            selectedQuad = index;
            break;
        }
    }
    if (selectedQuad < 0 || selectedQuad >= surfaceQuads.size())
        return false;

    QPointF surfacePoint;
    const bool mapped = surfaceQuads[selectedQuad].surfaceToCanvasTransform()
                            .mapInverse(point, &surfacePoint);
    *result = surfacePoint;
    return mapped && std::isfinite(result->x()) && std::isfinite(result->y());
}
