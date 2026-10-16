#include "marqueetool.h"

#include <QPainter>
#include <QtMath>
#include <cmath>

namespace {
QSize extractionSize(const QRectF &rect)
{
    if (!qIsFinite(rect.width()) || !qIsFinite(rect.height()) || rect.width() < 1 || rect.height() < 1
        || rect.width() > 8192 || rect.height() > 8192)
        return {};
    const QSize size(qCeil(rect.width()), qCeil(rect.height()));
    return qint64(size.width()) * size.height() * 4 <= 64*1024*1024 ? size : QSize();
}
}

bool MarqueeTool::beginCreate(const QVector<PerspectivePlane> &planes, const QPointF &point)
{
    clear();
    const int planeIndex = topmostPlaneIndexAt(planes, point);
    if (planeIndex < 0 || !planes[planeIndex].quad().isProjectable())
        return false;
    const PerspectivePlane &host = planes[planeIndex];
    for (const PerspectivePlane &plane : planes) {
        if (plane.surfaceGroupId() == host.surfaceGroupId() && plane.quad().isProjectable()) {
            m_selectionFaces.append(plane.quad());
            m_selectionMappings.append(plane.quad().surfaceToCanvasTransform());
            QPainterPath path;
            path.addPolygon(plane.quad().surfacePolygon());
            path.closeSubpath();
            m_selectionFacePaths.append(path);
        }
    }
    if (!host.quad().surfaceToCanvasTransform().mapInverse(point, &m_selectionPressSurface)) {
        clear();
        return false;
    }
    m_selectionRect = QRectF(m_selectionPressSurface, QSizeF());
    m_selectionAction = Action::Create;
    return true;
}

bool MarqueeTool::beginMove(const QPointF &point)
{
    QPointF surface;
    if (!contains(point) || !mapToSurface(point, &surface))
        return false;
    end();
    m_selectionPressSurface = surface;
    m_selectionStartRect = m_selectionRect;
    m_selectionAction = Action::Move;
    return true;
}

bool MarqueeTool::beginFill(const QPointF &point, const QImage &sampleSource,
                           const QImage &paintBefore)
{
    if (sampleSource.isNull() || paintBefore.isNull() || !beginMove(point))
        return false;
    m_selectionAction = Action::Fill;
    m_selectionSampleSource = sampleSource;
    m_selectionFillOffset = QPointF();
    return true;
}

void MarqueeTool::end()
{
    if (m_selectionRect.width() < 1 || m_selectionRect.height() < 1) {
        clear();
        return;
    }
    m_selectionAction = Action::None;
    m_selectionSampleSource = QImage();
}

bool MarqueeTool::contains(const QPointF &point) const
{
    return !m_selectionRect.isEmpty() && outline().contains(point);
}

FloatingSelection MarqueeTool::extractedSelection(const QImage &bitmap, int hostFace) const
{
    FloatingSelection image;
    image.bitmap = bitmap;
    image.unhealedBitmap = m_healMode ? bitmap : QImage();
    image.opacity = m_opacity;
    image.healMode = m_healMode;
    image.placementOrigin = extractionRect().topLeft();
    const QRectF logical = m_selectionRect.normalized().translated(-image.placementOrigin);
    const qreal sx = qreal(bitmap.width()) / qCeil(extractionRect().width());
    const qreal sy = qreal(bitmap.height()) / qCeil(extractionRect().height());
    image.selectionRectInBitmap = QRectF(logical.x() * sx, logical.y() * sy,
                                         logical.width() * sx, logical.height() * sy);
    image.surfaceAttached = true;
    image.surfaceQuads = m_selectionFaces;
    image.hostQuadIndex = hostFace;
    return image;
}

QRectF MarqueeTool::extractionRect() const
{
    // Feather is a Gaussian selection mask, including its outward falloff.
    const int margin = 2 * m_feather;
    return m_selectionRect.normalized().adjusted(-margin, -margin, margin, margin);
}

void MarqueeTool::featherPixels(QImage &bitmap, const QRectF &rect) const
{
    if (m_feather == 0 || bitmap.isNull()) return;
    const QRectF selection = m_selectionRect.normalized();
    const qreal sigma = m_feather / 2.0;
    const qreal denominator = std::sqrt(2.0) * sigma;
    QVector<qreal> horizontal(bitmap.width()), vertical(bitmap.height());
    const auto coverage = [denominator](qreal p, qreal start, qreal end) {
        return .5 * (std::erf((p - start) / denominator) - std::erf((p - end) / denominator));
    };
    for (int x = 0; x < bitmap.width(); ++x)
        horizontal[x] = coverage(rect.left() + (x + .5) * rect.width() / bitmap.width(), selection.left(), selection.right());
    for (int y = 0; y < bitmap.height(); ++y)
        vertical[y] = coverage(rect.top() + (y + .5) * rect.height() / bitmap.height(), selection.top(), selection.bottom());
    for (int y = 0; y < bitmap.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(bitmap.scanLine(y));
        for (int x = 0; x < bitmap.width(); ++x) {
            const qreal mask = horizontal[x] * vertical[y];
            const QRgb p = row[x];
            row[x] = qRgba(qRound(qRed(p) * mask), qRound(qGreen(p) * mask),
                           qRound(qBlue(p) * mask), qRound(qAlpha(p) * mask));
        }
    }
}

void MarqueeTool::clear()
{
    m_selectionFaces.clear();
    m_selectionMappings.clear();
    m_selectionFacePaths.clear();
    m_outline = QPainterPath();
    m_outlineValid = false;
    m_selectionRect = QRectF();
    m_selectionStartRect = QRectF();
    m_selectionPressSurface = QPointF();
    m_selectionFillOffset = QPointF();
    m_selectionAction = Action::None;
    m_selectionSampleSource = QImage();
}

// 把画面上的点换算到选区所在曲面的展开坐标：优先用包住它的那个面片，
// 都不包住时退回第一个面片外推。选区因此可以跨越共享曲面的多个平面。
bool MarqueeTool::mapToSurface(const QPointF &point, QPointF *surface) const
{
    if (!surface || m_selectionFaces.isEmpty())
        return false;
    for (int i = m_selectionFaces.size() - 1; i >= 0; --i) {
        const PerspectiveQuad &face = m_selectionFaces[i];
        if (!face.containsCanvasPoint(point))
            continue;
        if (m_selectionMappings[i].mapInverse(point, surface))
            return true;
    }
    return m_selectionMappings.first().mapInverse(point, surface);
}

// 选区轮廓：把展开坐标下的矩形按面片切开再逐片投影回画面，
// 于是跨接缝的选区画出来会沿折线拐弯，而不是一个平面矩形。
QPainterPath MarqueeTool::outline() const
{
    if (m_outlineValid && m_outlineRect == m_selectionRect)
        return m_outline;
    ++m_outlineBuildCount;
    m_outlineRect = m_selectionRect;
    m_outlineValid = true;
    m_outline = QPainterPath();
    QPainterPath result;
    if (m_selectionRect.isEmpty())
        return result;
    QPainterPath rectangle;
    rectangle.addRect(m_selectionRect.normalized());
    for (int index = 0; index < m_selectionFaces.size(); ++index) {
        const QPainterPath clipped = rectangle.intersected(m_selectionFacePaths[index]);
        const PerspectiveTransform &mapping = m_selectionMappings[index];
        if (!mapping.isValid())
            continue;
        for (const QPolygonF &surfacePolygon : clipped.toFillPolygons()) {
            QPolygonF canvasPolygon;
            for (const QPointF &surfacePoint : surfacePolygon) {
                QPointF canvasPoint;
                if (mapping.mapForward(surfacePoint, &canvasPoint))
                    canvasPolygon.append(canvasPoint);
            }
            if (canvasPolygon.size() >= 3) {
                QPainterPath patch;
                patch.addPolygon(canvasPolygon);
                patch.closeSubpath();
                result = result.united(patch);
            }
        }
    }
    m_outline = result;
    return m_outline;
}

// 拖动过程中的选区：新建时从按下点拉到光标（Shift 取正方形），平移时整体位移
// （Shift 锁单轴并对齐网格），Ctrl 克隆则持续把光标处的内容补进选区。
QRect MarqueeTool::update(const QPointF &point, Qt::KeyboardModifiers modifiers,
                         int gridSize, QImage *paintLayer)
{
    Q_UNUSED(paintLayer);
    QPointF surface;
    if (!mapToSurface(point, &surface))
        return {};
    if (m_selectionAction == Action::Create) {
        QPointF delta = surface - m_selectionPressSurface;
        if (modifiers & Qt::ShiftModifier) {
            const qreal side = qMax(qAbs(delta.x()), qAbs(delta.y()));
            delta.setX(delta.x() < 0 ? -side : side);
            delta.setY(delta.y() < 0 ? -side : side);
        }
        m_selectionRect = QRectF(m_selectionPressSurface,
                                 m_selectionPressSurface + delta).normalized();
    } else if (m_selectionAction == Action::Move) {
        QPointF delta = surface - m_selectionPressSurface;
        if (modifiers & Qt::ShiftModifier) {
            // 锁到位移较大的那个轴，再吸附到网格
            if (qAbs(delta.x()) >= qAbs(delta.y()))
                delta.setY(0);
            else
                delta.setX(0);
            // Keep the host Qt 5 half-grid tie rule on Qt 6 as well.
            delta.setX(std::floor(delta.x() / qMax(1, gridSize) + .5) * qMax(1, gridSize));
            delta.setY(std::floor(delta.y() / qMax(1, gridSize) + .5) * qMax(1, gridSize));
        }
        m_selectionRect = m_selectionStartRect.translated(delta);
    } else if (m_selectionAction == Action::Fill) {
        m_selectionFillOffset = surface - m_selectionPressSurface;
    }
    return {};
}

// Alt 拖动：把选区里的内容（背景 + 绘画层 + 浮动选区）抠成一张浮动选区，
// 并吸附到选区所在的同一组面片上，于是它还能继续被拖动、缩放。
FloatingSelection MarqueeTool::copy(const QImage &source) const
{
    QPointF point;
    if (m_selectionFaces.isEmpty()
        || !m_selectionFaces.first().surfaceToCanvasTransform().mapForward(m_selectionPressSurface, &point))
        return {};
    return copy(source, point);
}

FloatingSelection MarqueeTool::copy(const QImage &source, const QPointF &point) const
{
    const QRectF rect = extractionRect();
    if (source.isNull() || m_selectionRect.width() < 1 || m_selectionRect.height() < 1 || m_selectionFaces.isEmpty())
        return {};
    const QSize size = extractionSize(rect);
    if (size.isEmpty())
        return {};

    QImage extracted(size, QImage::Format_ARGB32_Premultiplied);
    if (extracted.isNull())
        return {};
    extracted.fill(Qt::transparent);
    QPainter painter(&extracted);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    int hostFace = 0;
    QPointF pressSurface;
    mapToSurface(point, &pressSurface);
    for (int i = 0; i < m_selectionFaces.size(); ++i) {
        const PerspectiveQuad &face = m_selectionFaces[i];
        QPolygonF target;
        for (int c = 0; c < 4; ++c)
            target.append(face.surfaceCorners()[c] - rect.topLeft());
        QPainterPath clip;
        clip.addPolygon(target);
        clip.closeSubpath();
        QPainterPath outputBounds;
        outputBounds.addRect(QRectF(QPointF(), QSizeF(size)));
        clip = outputBounds.intersected(clip);
        const PerspectiveTransform mapping(face.canvasPolygon(), target);
        if (!mapping.isValid() || clip.isEmpty())
            continue;
        const QRect sourceRoi = face.canvasPolygon().boundingRect().adjusted(-2,-2,2,2)
                                    .toAlignedRect().intersected(source.rect());
        if (sourceRoi.isEmpty()) continue;
        painter.save();
        painter.setClipPath(clip);
        painter.setWorldTransform(mapping.forward());
        painter.drawImage(sourceRoi.topLeft(), source, sourceRoi);
        painter.restore();
        if (face.surfacePolygon().containsPoint(pressSurface, Qt::OddEvenFill))
            hostFace = i;
    }
    painter.end();
    const QImage original = extracted;
    featherPixels(extracted, rect);
    FloatingSelection result = extractedSelection(extracted, hostFace);
    result.unfeatheredBitmap = original;
    result.feather = m_feather;
    return result;
}

// Ctrl 拖动只生成独立图像；预览可降低取样分辨率，松手时按原分辨率生成。
FloatingSelection MarqueeTool::clone(qreal rasterScale) const
{
    const QRectF rect = extractionRect();
    if (m_selectionSampleSource.isNull() || m_selectionFaces.isEmpty()
        || m_selectionRect.width() < 1 || m_selectionRect.height() < 1) {
        return {};
    }
    const QSize fullSize = extractionSize(rect);
    const QSize size(qMax(1, qCeil(fullSize.width() * qBound(.0001, rasterScale, 1.0))),
                     qMax(1, qCeil(fullSize.height() * qBound(.0001, rasterScale, 1.0))));
    if (fullSize.isEmpty())
        return {};
    QImage extracted(size, QImage::Format_ARGB32_Premultiplied);
    if (extracted.isNull())
        return {};
    extracted.fill(Qt::transparent);
    QPainter painter(&extracted);
    painter.scale(qreal(size.width()) / fullSize.width(), qreal(size.height()) / fullSize.height());
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const QPointF origin = rect.topLeft();
    QPainterPath bounds; // 位图范围，等价于选区矩形
    bounds.addRect(QRectF(QPointF(), QSizeF(fullSize)));
    int hostFace = 0;
    for (int i = 0; i < m_selectionFaces.size(); ++i) {
        if (m_selectionFaces[i].surfacePolygon()
                .containsPoint(m_selectionPressSurface, Qt::OddEvenFill)) {
            hostFace = i;
        }
    }
    for (const PerspectiveQuad &targetFace : m_selectionFaces) {
        QPolygonF targetQuad;
        for (int c = 0; c < 4; ++c)
            targetQuad.append(targetFace.surfaceCorners()[c] - origin);
        QPainterPath targetSurface;
        targetSurface.addPolygon(targetQuad);
        targetSurface.closeSubpath();
        const QPainterPath targetClip = bounds.intersected(targetSurface);
        if (targetClip.isEmpty()) continue;
        const QRectF targetBounds = targetClip.boundingRect();
        for (const PerspectiveQuad &sourceFace : m_selectionFaces) {
            QPolygonF shifted; // 源面按拖动偏移搬到目标位置后，在位图中的四边形
            for (int c = 0; c < 4; ++c)
                shifted.append(sourceFace.surfaceCorners()[c] - m_selectionFillOffset - origin);
            if (!targetBounds.intersects(shifted.boundingRect())) continue;
            QPainterPath sourceDomain;
            sourceDomain.addPolygon(shifted);
            sourceDomain.closeSubpath();
            const QPainterPath clip = targetClip.intersected(sourceDomain);
            if (clip.isEmpty())
                continue;
            QPolygonF sourceCanvas;
            for (int c = 0; c < 4; ++c)
                sourceCanvas.append(sourceFace.canvasCorners()[c]);
            const PerspectiveTransform mapping(sourceCanvas, shifted);
            if (!mapping.isValid())
                continue;
            const QRect sourceRoi = sourceCanvas.boundingRect().adjusted(-2,-2,2,2)
                                        .toAlignedRect().intersected(m_selectionSampleSource.rect());
            if (sourceRoi.isEmpty()) continue;
            painter.save();
            painter.setClipPath(clip);
            painter.setWorldTransform(mapping.forward(), true);
            painter.drawImage(sourceRoi.topLeft(), m_selectionSampleSource, sourceRoi);
            painter.restore();
        }
    }
    painter.end();
    const QImage original = extracted;
    featherPixels(extracted, rect);
    FloatingSelection result = extractedSelection(extracted, hostFace);
    result.unfeatheredBitmap = original;
    result.feather = m_feather;
    result.scaleFactors = QPointF(qreal(fullSize.width()) / size.width(), qreal(fullSize.height()) / size.height());
    return result;
}
