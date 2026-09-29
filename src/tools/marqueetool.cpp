#include "marqueetool.h"

#include <QPainter>
#include <QtMath>

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
    if (planeIndex < 0)
        return false;
    const PerspectivePlane &host = planes[planeIndex];
    for (const PerspectivePlane &plane : planes) {
        if (plane.surfaceGroupId() == host.surfaceGroupId())
            m_selectionFaces.append(plane.quad());
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
    m_selectionPaintBefore = paintBefore;
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
    m_selectionPaintBefore = QImage();
}

bool MarqueeTool::contains(const QPointF &point) const
{
    return !m_selectionRect.isEmpty() && outline().contains(point);
}

FloatingImage MarqueeTool::extractedImage(const QImage &bitmap, int hostFace) const
{
    FloatingImage image;
    image.bitmap = bitmap;
    image.placementOrigin = m_selectionRect.normalized().topLeft();
    image.surfaceAttached = true;
    image.surfaceQuads = m_selectionFaces;
    image.hostQuadIndex = hostFace;
    return image;
}

void MarqueeTool::clear()
{
    m_selectionFaces.clear();
    m_selectionRect = QRectF();
    m_selectionStartRect = QRectF();
    m_selectionPressSurface = QPointF();
    m_selectionFillOffset = QPointF();
    m_selectionAction = Action::None;
    m_selectionSampleSource = QImage();
    m_selectionPaintBefore = QImage();
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
        if (face.surfaceToCanvasTransform().mapInverse(point, surface))
            return true;
    }
    return m_selectionFaces.first().surfaceToCanvasTransform().mapInverse(point, surface);
}

// 选区轮廓：把展开坐标下的矩形按面片切开再逐片投影回画面，
// 于是跨接缝的选区画出来会沿折线拐弯，而不是一个平面矩形。
QPainterPath MarqueeTool::outline() const
{
    QPainterPath result;
    if (m_selectionRect.isEmpty())
        return result;
    QPainterPath rectangle;
    rectangle.addRect(m_selectionRect.normalized());
    for (const PerspectiveQuad &face : m_selectionFaces) {
        QPainterPath facePath;
        facePath.addPolygon(face.surfacePolygon());
        facePath.closeSubpath();
        const QPainterPath clipped = rectangle.intersected(facePath);
        const PerspectiveTransform mapping = face.surfaceToCanvasTransform();
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
    return result;
}

// 拖动过程中的选区：新建时从按下点拉到光标（Shift 取正方形），平移时整体位移
// （Shift 锁单轴并对齐网格），Ctrl 克隆则持续把光标处的内容补进选区。
QRect MarqueeTool::update(const QPointF &point, Qt::KeyboardModifiers modifiers,
                         int gridSize, QImage *paintLayer)
{
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
            delta.setX(qRound(delta.x() / qMax(1, gridSize)) * qMax(1, gridSize));
            delta.setY(qRound(delta.y() / qMax(1, gridSize)) * qMax(1, gridSize));
        }
        m_selectionRect = m_selectionStartRect.translated(delta);
    } else if (m_selectionAction == Action::Fill && paintLayer) {
        return fillFromPoint(point, *paintLayer);
    }
    return {};
}

// Ctrl 克隆：把「按下时光标下方的区域」按单应搬进选区。每帧都从按下前的绘画层
// 重建，避免重复移动把上一次的预览也当成取样内容叠上去。
QRect MarqueeTool::fillFromPoint(const QPointF &point, QImage &paintLayer)
{
    if (m_selectionSampleSource.isNull() || m_selectionPaintBefore.isNull())
        return {};

    QPointF sourceAnchor;
    if (!mapToSurface(point, &sourceAnchor))
        return {};
    const QPointF sourceOffset = sourceAnchor - m_selectionPressSurface;
    m_selectionFillOffset = sourceOffset;
    const QPainterPath targetPath = outline();
    const QRect dirty = targetPath.boundingRect().toAlignedRect().adjusted(-1, -1, 1, 1)
                            .intersected(paintLayer.rect());
    if (dirty.isEmpty())
        return {};

    QPainter painter(&paintLayer);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(dirty, m_selectionPaintBefore, dirty);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QPainterPath selectionSurfacePath;
    selectionSurfacePath.addRect(m_selectionRect.normalized());
    // 每个「目标面 + 源面」组合是一片单应补丁，整片交给光栅器一次画完。
    for (const PerspectiveQuad &targetFace : m_selectionFaces) {
        QPainterPath targetSurfacePath;
        targetSurfacePath.addPolygon(targetFace.surfacePolygon());
        targetSurfacePath.closeSubpath();
        const PerspectiveTransform targetMapping = targetFace.surfaceToCanvasTransform();
        if (!targetMapping.isValid())
            continue;
        for (const PerspectiveQuad &sourceFace : m_selectionFaces) {
            QPolygonF shiftedSourceSurface;
            QPolygonF sourceCanvas;
            QPolygonF targetCanvas;
            for (int c = 0; c < 4; ++c) {
                shiftedSourceSurface.append(sourceFace.surfaceCorners()[c] - sourceOffset);
                sourceCanvas.append(sourceFace.canvasCorners()[c]);
                QPointF mapped;
                if (!targetMapping.mapForward(shiftedSourceSurface.last(), &mapped)) {
                    targetCanvas.clear();
                    break;
                }
                targetCanvas.append(mapped);
            }
            if (targetCanvas.size() != 4)
                continue;

            QPainterPath sourceDomain;
            sourceDomain.addPolygon(shiftedSourceSurface);
            sourceDomain.closeSubpath();
            const QPainterPath surfacePatch = selectionSurfacePath
                                                  .intersected(targetSurfacePath)
                                                  .intersected(sourceDomain);
            if (surfacePatch.isEmpty())
                continue;

            QPainterPath canvasClip;
            for (const QPolygonF &polygon : surfacePatch.toFillPolygons()) {
                QPolygonF mappedPolygon;
                for (const QPointF &surfacePoint : polygon) {
                    QPointF mapped;
                    if (targetMapping.mapForward(surfacePoint, &mapped))
                        mappedPolygon.append(mapped);
                }
                if (mappedPolygon.size() >= 3) {
                    canvasClip.addPolygon(mappedPolygon);
                    canvasClip.closeSubpath();
                }
            }
            const PerspectiveTransform sourceToTarget(sourceCanvas, targetCanvas);
            if (!sourceToTarget.isValid() || canvasClip.isEmpty())
                continue;
            painter.save();
            painter.setClipPath(canvasClip, Qt::IntersectClip);
            painter.setWorldTransform(sourceToTarget.forward());
            painter.drawImage(QPointF(), m_selectionSampleSource);
            painter.restore();
        }
    }
    painter.end();
    return dirty;
}

// Alt 拖动：把选区里的内容（背景 + 绘画层 + 浮动图像）抠成一张浮动图像，
// 并吸附到选区所在的同一组面片上，于是它还能继续被拖动、缩放。
FloatingImage MarqueeTool::copy(const QImage &source, const QPointF &point) const
{
    const QRectF rect = m_selectionRect.normalized();
    if (source.isNull() || rect.width() < 1 || rect.height() < 1 || m_selectionFaces.isEmpty())
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
        painter.save();
        painter.setClipPath(clip);
        painter.setWorldTransform(mapping.forward());
        painter.drawImage(QPointF(), source);
        painter.restore();
        if (face.surfacePolygon().containsPoint(pressSurface, Qt::OddEvenFill))
            hostFace = i;
    }
    painter.end();
    return extractedImage(extracted, hostFace);
}

// Ctrl 拖动的结果不留在绘画层，而是落成一张浮动图像：与 fillFromPoint
// 共用同一套「源面 → 目标面」单应，因此松手前后看到的像素不会跳变。
FloatingImage MarqueeTool::clone() const
{
    const QRectF rect = m_selectionRect.normalized();
    if (m_selectionSampleSource.isNull() || m_selectionFaces.isEmpty()
        || rect.width() < 1 || rect.height() < 1) {
        return {};
    }
    const QSize size = extractionSize(rect);
    if (size.isEmpty())
        return {};
    QImage extracted(size, QImage::Format_ARGB32_Premultiplied);
    if (extracted.isNull())
        return {};
    extracted.fill(Qt::transparent);
    QPainter painter(&extracted);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const QPointF origin = rect.topLeft();
    QPainterPath bounds; // 位图范围，等价于选区矩形
    bounds.addRect(QRectF(QPointF(), QSizeF(size)));
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
        for (const PerspectiveQuad &sourceFace : m_selectionFaces) {
            QPolygonF shifted; // 源面按拖动偏移搬到目标位置后，在位图中的四边形
            for (int c = 0; c < 4; ++c)
                shifted.append(sourceFace.surfaceCorners()[c] - m_selectionFillOffset - origin);
            QPainterPath sourceDomain;
            sourceDomain.addPolygon(shifted);
            sourceDomain.closeSubpath();
            const QPainterPath clip = bounds.intersected(targetSurface).intersected(sourceDomain);
            if (clip.isEmpty())
                continue;
            QPolygonF sourceCanvas;
            for (int c = 0; c < 4; ++c)
                sourceCanvas.append(sourceFace.canvasCorners()[c]);
            const PerspectiveTransform mapping(sourceCanvas, shifted);
            if (!mapping.isValid())
                continue;
            painter.save();
            painter.setClipPath(clip);
            painter.setWorldTransform(mapping.forward());
            painter.drawImage(QPointF(), m_selectionSampleSource);
            painter.restore();
        }
    }
    painter.end();
    return extractedImage(extracted, hostFace);
}
