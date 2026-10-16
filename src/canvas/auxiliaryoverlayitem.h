#pragma once

#include "../core/perspectiveplane.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickPaintedItem>
#include <QPainter>
#include <QPicture>
#include <QSGClipNode>
#include <QSGGeometryNode>
#include <QSGRendererInterface>
#include <QSGVertexColorMaterial>
#include <algorithm>
#include <cmath>

// One software overlay replays cached commands; no image content is repainted.
class AuxiliaryRasterItem final : public QQuickPaintedItem
{
public:
    explicit AuxiliaryRasterItem(QQuickItem *parent) : QQuickPaintedItem(parent)
    {
        setAcceptedMouseButtons(Qt::NoButton);
        setVisible(false);
        setAntialiasing(true);
    }
    QVector<QPicture> pictures;
    void paint(QPainter *painter) override
    {
        for (const QPicture &picture : pictures)
            painter->drawPicture(QPointF(), picture);
    }
};

// A shared layer, with independently cached plane, extrusion and creation nodes.
// GUI-side snapshots are consumed by updatePaintNode while the GUI is blocked.
class AuxiliaryOverlayItem final : public QQuickItem
{
    using Vertices = QVector<QSGGeometry::ColoredPoint2D>;
    struct Batch {
        quint64 revision = 0;
        Vertices outline, grid, handles;
        QPolygonF clip;
        QPicture picture;
    };
    struct PlaneCache {
        bool valid = false, selected = false, handles = false, extrudeHandles = false, highlighted = false;
        PerspectivePlane plane;
        int draggedControlPoint = -1;
        qreal scale = 0, gridSize = 0, dpr = 0;
        QPointF offset;
        QSize backgroundSize;
        Batch batch;
    };
    struct GeometryNode : QSGGeometryNode {
        GeometryNode()
        {
            auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            setGeometry(geometry);
            setFlag(OwnsGeometry);
            setMaterial(new QSGVertexColorMaterial);
            setFlag(OwnsMaterial);
        }
        void assign(const Vertices &vertices)
        {
            geometry()->allocate(vertices.size());
            std::copy(vertices.cbegin(), vertices.cend(), geometry()->vertexDataAsColoredPoint2D());
            markDirty(DirtyGeometry);
        }
    };
    struct ClipNode : QSGClipNode {
        QSGGeometry clipGeometry{QSGGeometry::defaultAttributes_Point2D(), 4};
        ClipNode()
        {
            clipGeometry.setDrawingMode(QSGGeometry::DrawTriangleFan);
            setGeometry(&clipGeometry);
            setIsRectangular(false);
        }
        void assign(const QPolygonF &polygon)
        {
            auto *vertices = clipGeometry.vertexDataAsPoint2D();
            for (int i = 0; i < 4; ++i) {
                const QPointF point = i < polygon.size() ? polygon[i] : QPointF();
                vertices[i].set(float(point.x()), float(point.y()));
            }
            markDirty(DirtyGeometry);
        }
    };
    struct BatchNode : QSGNode {
        quint64 revision = 0;
        GeometryNode *outline = nullptr;
        ClipNode *clip = nullptr;
        GeometryNode *grid = nullptr;
        GeometryNode *handles = nullptr;
        void assign(const Batch &batch)
        {
            if (revision == batch.revision)
                return;
            revision = batch.revision;
            if (!batch.outline.isEmpty()) {
                if (!outline) {
                    outline = new GeometryNode;
                    if (firstChild()) insertChildNodeBefore(outline, firstChild());
                    else appendChildNode(outline);
                }
                outline->assign(batch.outline);
            } else if (outline) {
                removeChildNode(outline);
                delete outline;
                outline = nullptr;
            }
            // Never leave an empty/degenerate clip node in the render tree.
            if (!batch.grid.isEmpty()) {
                if (!clip) {
                    clip = new ClipNode;
                    grid = new GeometryNode;
                    clip->appendChildNode(grid);
                    if (handles) insertChildNodeBefore(clip, handles);
                    else appendChildNode(clip);
                }
                grid->assign(batch.grid);
                clip->assign(batch.clip);
            } else if (clip) {
                removeChildNode(clip);
                delete clip;
                clip = nullptr;
                grid = nullptr;
            }
            if (!batch.handles.isEmpty()) {
                if (!handles) {
                    handles = new GeometryNode;
                    appendChildNode(handles);
                }
                handles->assign(batch.handles);
            } else if (handles) {
                removeChildNode(handles);
                delete handles;
                handles = nullptr;
            }
        }
    };
    struct OverlayNode : QSGNode {
        QSGNode *planes = new QSGNode;
        BatchNode *extrusion = new BatchNode;
        BatchNode *creation = new BatchNode;
        BatchNode *selection = new BatchNode;
        OverlayNode()
        {
            appendChildNode(planes);
            appendChildNode(extrusion);
            appendChildNode(creation);
            appendChildNode(selection);
        }
    };

public:
    explicit AuxiliaryOverlayItem(QQuickItem *parent) : QQuickItem(parent)
    {
        setFlag(ItemHasContents, true);
        setAcceptedMouseButtons(Qt::NoButton);
        setZ(1);
        setClip(true);
        m_raster = new AuxiliaryRasterItem(this);
    }

    // Useful for checking that hover/unchanged planes do not rebuild grid data.
    quint64 planeGeometryBuildCount() const { return m_planeBuildCount; }

    void setSelectionGuides(const QPainterPath &marquee, const QPainterPath &floating,
                            const QVector<QPointF> &handles, qreal scale, const QPointF &offset)
    {
        const qreal dpr = window() ? window()->devicePixelRatio() : 1;
        if (marquee == m_marqueePath && floating == m_floatingPath && handles == m_selectionHandles
            && scale == m_selectionScale && offset == m_selectionOffset && dpr == m_selectionDpr)
            return;
        m_marqueePath = marquee; m_floatingPath = floating; m_selectionHandles = handles;
        m_selectionScale = scale; m_selectionOffset = offset; m_selectionDpr = dpr;
        m_selection = Batch(); m_selection.revision = ++m_revision;
        const QTransform view(scale, 0, 0, scale, offset.x(), offset.y());
        QPainter painter(&m_selection.picture);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        for (const QPainterPath &canvasPath : {floating, marquee}) {
            const QPainterPath path = view.map(canvasPath);
            QPen pen(Qt::white, 1); pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen); painter.drawPath(path);
            pen.setColor(Qt::black); pen.setDashPattern({4, 4});
            painter.setPen(pen); painter.drawPath(path);
            for (const QPolygonF &polygon : path.toSubpathPolygons()) {
                for (int i = 1; i < polygon.size(); ++i)
                    line(m_selection.outline, polygon[i - 1], polygon[i], Qt::white, 1, 1 / dpr);
                qreal traveled = 0;
                for (int i = 1; i < polygon.size(); ++i) {
                    const QPointF a = polygon[i - 1], delta = polygon[i] - a;
                    const qreal length = std::hypot(delta.x(), delta.y());
                    if (length < 1e-6) continue;
                    qreal low = 0, high = 1;
                    const QRectF viewport = QRectF(QPointF(), size()).adjusted(-4, -4, 4, 4);
                    auto clipAxis = [&](qreal start, qreal change, qreal min, qreal max) {
                        if (qAbs(change) < 1e-12) return start >= min && start <= max;
                        qreal x = (min - start) / change, y = (max - start) / change;
                        if (x > y) std::swap(x, y);
                        low = qMax(low, x); high = qMin(high, y);
                        return low <= high;
                    };
                    const bool visible = clipAxis(a.x(), delta.x(), viewport.left(), viewport.right())
                        && clipAxis(a.y(), delta.y(), viewport.top(), viewport.bottom());
                    qreal at = low * length;
                    const qreal end = high * length;
                    while (visible && at < end - 1e-6) {
                        const qreal distance = traveled + at;
                        const qreal dash = std::floor((distance + 1e-6) / 4);
                        const qreal remaining = qMax(1e-6, (dash + 1) * 4 - distance);
                        const qreal count = qMin(remaining, end - at);
                        if (qint64(dash) % 2 == 0)
                            line(m_selection.outline, a + delta * (at / length),
                                 a + delta * ((at + count) / length), Qt::black, 1, 1 / dpr);
                        at += count;
                    }
                    traveled += length;
                }
            }
        }
        for (const QPointF &point : handles)
            handle(m_selection.handles, painter, view.map(point), QColor("#1769aa"), Qt::white, 4.5);
        painter.end(); refresh();
    }

    void setPlaneGuides(const QVector<PerspectivePlane> &planes, int selected,
                        bool handles, bool extrudeHandles, const PerspectivePlane *extrusion,
                        qreal gridSize, qreal scale, const QPointF &offset, const QSize &backgroundSize = {},
                        int draggedControlPoint = -1, int highlightedPlane = -1)
    {
        const qreal dpr = window() ? window()->devicePixelRatio() : 1;
        bool changed = m_planes.size() != planes.size();
        m_planes.resize(planes.size());
        for (int i = 0; i < planes.size(); ++i)
            changed = updatePlane(m_planes[i], planes[i], i == selected, handles,
                                  extrudeHandles, gridSize, scale, offset, dpr, backgroundSize,
                                  i == selected ? draggedControlPoint : -1, i == highlightedPlane) || changed;
        if (extrusion) {
            changed = updatePlane(m_extrusion, *extrusion, true, false, false,
                                  gridSize, scale, offset, dpr, backgroundSize) || changed;
        } else if (m_extrusion.valid) {
            m_extrusion.valid = false;
            m_extrusion.batch = Batch();
            m_extrusion.batch.revision = ++m_revision;
            changed = true;
        }
        if (changed)
            refresh();
    }

    void setCreationPreview(const QVector<QPointF> &points, const QPointF &cursor,
                            bool visible, qreal scale, const QPointF &offset, const QSize &backgroundSize = {})
    {
        const bool cursorVisible = visible && !points.isEmpty() && points.size() < 4;
        const qreal dpr = window() ? window()->devicePixelRatio() : 1;
        if (points == m_creationPoints && cursorVisible == m_cursorVisible
            && (!cursorVisible || cursor == m_cursor) && scale == m_creationScale
            && offset == m_creationOffset && dpr == m_creationDpr && backgroundSize == m_creationBackgroundSize)
            return;
        m_creationPoints = points;
        m_cursor = cursor;
        m_cursorVisible = cursorVisible;
        m_creationScale = scale;
        m_creationOffset = offset;
        m_creationDpr = dpr;
        m_creationBackgroundSize = backgroundSize;
        m_creation = Batch();
        m_creation.revision = ++m_revision;
        QVector<QPointF> screen;
        for (const QPointF &point : points)
            screen.append(offset + point * scale);
        const QPointF end = offset + cursor * scale;
        QPainter painter(&m_creation.picture);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor creationColor("#017AFF");
        painter.setPen(QPen(creationColor, 2));
        auto drawLine = [&](const QPointF &a, const QPointF &b) {
            line(m_creation.outline, a, b, creationColor, 2, 1 / dpr);
            painter.drawLine(a, b);
        };
        for (int i = 1; i < screen.size(); ++i)
            drawLine(screen[i - 1], screen[i]);
        if (cursorVisible) {
            drawLine(screen.last(), end);
            if (screen.size() == 3)
                drawLine(end, screen.first());
        }
        for (const QPointF &point : screen)
            handle(m_creation.handles, painter, point);
        if (cursorVisible)
            handle(m_creation.handles, painter, end);
        painter.end();
        refresh();
    }

protected:
    void geometryChange(const QRectF &geometry, const QRectF &previous) override
    {
        QQuickItem::geometryChange(geometry, previous);
        if (m_raster && geometry.size() != previous.size())
            m_raster->setSize(geometry.size());
    }

    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override
    {
        if (m_software) {
            delete oldNode;
            return nullptr;
        }
        auto *node = static_cast<OverlayNode *>(oldNode);
        if (!node)
            node = new OverlayNode;
        while (node->planes->childCount() > m_planes.size()) {
            QSGNode *last = node->planes->lastChild();
            node->planes->removeChildNode(last);
            delete last;
        }
        while (node->planes->childCount() < m_planes.size())
            node->planes->appendChildNode(new BatchNode);
        QSGNode *child = node->planes->firstChild();
        for (const PlaneCache &plane : m_planes) {
            static_cast<BatchNode *>(child)->assign(plane.batch);
            child = child->nextSibling();
        }
        node->extrusion->assign(m_extrusion.batch);
        node->creation->assign(m_creation);
        node->selection->assign(m_selection);
        return node;
    }

private:
    bool updatePlane(PlaneCache &cache, const PerspectivePlane &plane, bool selected,
                     bool handles, bool extrudeHandles, qreal gridSize, qreal scale,
                     const QPointF &offset, qreal dpr, const QSize &backgroundSize, int draggedControlPoint = -1, bool highlighted = false)
    {
        handles = handles && selected;
        draggedControlPoint = handles ? draggedControlPoint : -1;
        extrudeHandles = handles && extrudeHandles;
        gridSize = selected ? gridSize : 0;
        if (cache.valid && cache.plane == plane && cache.selected == selected && cache.highlighted == highlighted
            && cache.handles == handles && cache.extrudeHandles == extrudeHandles
            && cache.draggedControlPoint == draggedControlPoint
            && cache.gridSize == gridSize && cache.scale == scale
            && cache.offset == offset && cache.dpr == dpr && cache.backgroundSize == backgroundSize)
            return false;
        cache.valid = true;
        cache.plane = plane;
        cache.selected = selected;
        cache.highlighted = highlighted;
        cache.handles = handles;
        cache.draggedControlPoint = draggedControlPoint;
        cache.extrudeHandles = extrudeHandles;
        cache.gridSize = gridSize;
        cache.scale = scale;
        cache.offset = offset;
        cache.dpr = dpr;
        cache.backgroundSize = backgroundSize;
        cache.batch = Batch();
        Batch &batch = cache.batch;
        batch.revision = ++m_revision;
        ++m_planeBuildCount;
        const auto &quad = plane.quad();
        for (const QPointF &point : quad.canvasPolygon())
            batch.clip.append(offset + point * scale);
        const QColor border = quad.guideColor(selected || highlighted, backgroundSize);
        const qreal width = highlighted ? 2.0 : selected ? 1.6 : 1.0;
        QPainter painter(&batch.picture);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(border, width));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(batch.clip);
        for (int i = 0; i < 4; ++i)
            line(batch.outline, batch.clip[i], batch.clip[(i + 1) % 4], border, width, 1 / dpr);
        if (selected && quad.isProjectable()) {
            QPainterPath clip;
            clip.addPolygon(batch.clip);
            clip.closeSubpath();
            painter.save();
            painter.setClipPath(clip);
            QColor color = border;
            color.setAlpha(145);
            painter.setPen(QPen(color, .8));
            const auto &canvas = quad.canvasCorners();
            const auto &surface = quad.surfaceCorners();
            const qreal safeGridSize = qMax(gridSize, 4.0 / qMax(scale, 1e-6));
            const int horizontal = int(qBound(1.0,
                (QLineF(canvas[0], canvas[1]).length() + QLineF(canvas[3], canvas[2]).length()) / (2 * safeGridSize), 256.0));
            const int vertical = int(qBound(1.0,
                (QLineF(canvas[0], canvas[3]).length() + QLineF(canvas[1], canvas[2]).length()) / (2 * safeGridSize), 256.0));
            const PerspectiveTransform projection = quad.surfaceToCanvasTransform();
            auto gridLine = [&](const QPointF &a, const QPointF &b) {
                QPointF start, end;
                if (!projection.mapForward(a, &start) || !projection.mapForward(b, &end))
                    return;
                start = offset + start * scale;
                end = offset + end * scale;
                line(batch.grid, start, end, color, .8, 1 / dpr);
                painter.drawLine(start, end);
            };
            for (int i = 1; i < horizontal; ++i) {
                const qreal t = qreal(i) / horizontal;
                gridLine(surface[0] + (surface[1] - surface[0]) * t,
                         surface[3] + (surface[2] - surface[3]) * t);
            }
            for (int i = 1; i < vertical; ++i) {
                const qreal t = qreal(i) / vertical;
                gridLine(surface[0] + (surface[3] - surface[0]) * t,
                         surface[1] + (surface[2] - surface[1]) * t);
            }
            painter.restore();
        }
        if (handles) {
            const auto points = quad.controlPoints();
            for (int i = 0; i < points.size(); ++i)
                if (plane.controlPointEditable(i, extrudeHandles)) {
                    const QPointF point = offset + points[i] * scale;
                    if (i == draggedControlPoint)
                        handle(batch.handles, painter, point, border, border, 4);
                    else
                        handle(batch.handles, painter, point);
                }
        }
        painter.end();
        return true;
    }

    void refresh()
    {
        m_software = window() && window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
        m_raster->setVisible(m_software);
        if (m_software) {
            m_raster->setSize(size());
            m_raster->pictures.clear();
            for (const PlaneCache &plane : m_planes)
                m_raster->pictures.append(plane.batch.picture);
            if (m_extrusion.valid)
                m_raster->pictures.append(m_extrusion.batch.picture);
            m_raster->pictures.append(m_creation.picture);
            m_raster->pictures.append(m_selection.picture);
            m_raster->update();
        }
        update();
    }

    static QSGGeometry::ColoredPoint2D vertex(const QPointF &point, const QColor &color)
    {
        QSGGeometry::ColoredPoint2D result;
        const int alpha = color.alpha();
        result.set(float(point.x()), float(point.y()), uchar(color.red() * alpha / 255),
                   uchar(color.green() * alpha / 255), uchar(color.blue() * alpha / 255), uchar(alpha));
        return result;
    }
    static void quad(Vertices &out, const QPointF &a, const QPointF &b,
                     const QPointF &c, const QPointF &d, const QColor &ab, const QColor &cd)
    {
        out << vertex(a, ab) << vertex(b, ab) << vertex(c, cd)
            << vertex(a, ab) << vertex(c, cd) << vertex(d, cd);
    }
    static void line(Vertices &out, const QPointF &a, const QPointF &b,
                     QColor color, qreal width, qreal fringe)
    {
        const QPointF delta = b - a;
        const qreal length = std::hypot(delta.x(), delta.y());
        if (length < 1e-6)
            return;
        const QPointF normal(-delta.y() / length, delta.x() / length);
        const QPointF tangent = delta / length * (width / 2);
        const QPointF start = a - tangent, end = b + tangent;
        const qreal innerWidth = qMax(0.0, (width - fringe) / 2);
        const qreal outerWidth = (width + fringe) / 2;
        color.setAlpha(qRound(color.alpha() * width / (innerWidth + outerWidth)));
        const QPointF inner = normal * innerWidth, outer = normal * outerWidth;
        if (innerWidth > 0)
            quad(out, start + inner, end + inner, end - inner, start - inner, color, color);
        QColor transparent = color;
        transparent.setAlpha(0);
        quad(out, start + inner, end + inner, end + outer, start + outer, color, transparent);
        quad(out, end - inner, start - inner, start - outer, end - outer, color, transparent);
    }
    static void handle(Vertices &out, QPainter &painter, const QPointF &point,
                       const QColor &border = QColor("#0e526e"), const QColor &fill = QColor("#f3f8fa"),
                       qreal outerHalfSize = 3)
    {
        for (const auto &layer : {qMakePair(outerHalfSize, border), qMakePair(outerHalfSize - 1, fill)}) {
            const qreal r = layer.first;
            quad(out, point + QPointF(-r, -r), point + QPointF(r, -r),
                 point + QPointF(r, r), point + QPointF(-r, r), layer.second, layer.second);
        }
        painter.setPen(QPen(border, 1));
        painter.setBrush(fill);
        const qreal radius = outerHalfSize - .5;
        painter.drawRect(QRectF(point - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)));
    }

    AuxiliaryRasterItem *m_raster = nullptr;
    QVector<PlaneCache> m_planes;
    PlaneCache m_extrusion;
    Batch m_creation;
    Batch m_selection;
    QPainterPath m_marqueePath, m_floatingPath;
    QVector<QPointF> m_selectionHandles;
    QPointF m_selectionOffset;
    qreal m_selectionScale = 0, m_selectionDpr = 0;
    QVector<QPointF> m_creationPoints;
    QPointF m_cursor, m_creationOffset;
    QSize m_creationBackgroundSize;
    bool m_cursorVisible = false, m_software = false;
    qreal m_creationScale = 0, m_creationDpr = 0;
    quint64 m_revision = 0, m_planeBuildCount = 0;
};
