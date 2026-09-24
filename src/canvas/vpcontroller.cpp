#include "vpcontroller.h"

#include "core/floatingimageprojection.h"
#include "core/scenerenderer.h"

#include <QDataStream>
#include <QIODevice>
#include <QPainter>

bool VpController::beginBrush(const QPointF &point)
{
    m_document.beginPaintTransaction();
    const QRect dirty = m_brushTool.begin(m_document.paintLayer(), m_document.planes(),
                                          m_document.background().size(), point);
    if (!m_brushTool.drawing())
        return false;
    m_document.addPaintDirty(dirty);
    return true;
}

void VpController::moveBrush(const QPointF &point)
{
    if (m_brushTool.drawing())
        m_document.addPaintDirty(m_brushTool.move(m_document.paintLayer(), point));
}

void VpController::endBrush()
{
    if (!m_brushTool.drawing())
        return;
    m_brushTool.end();
    m_document.commitHistory();
}

void VpController::renderBrushPreview(QPainter &painter, const QPointF &point) const
{
    m_brushTool.renderPreview(painter, m_document.planes(), m_document.background().size(), point);
}

const QImage &VpController::cloneSource()
{
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << m_document.background().cacheKey() << m_document.paintLayer().cacheKey()
           << qint64(m_document.floatingImages().size());
    for (const FloatingImage &image : m_document.floatingImages())
        stream << image.bitmap.cacheKey() << FloatingImageProjection::cacheKey(image);
    if (m_cloneTool.drawing() || (key == m_cloneSourceKey && !m_cloneSource.isNull()))
        return m_cloneSource;
    m_cloneSourceKey = key;
    m_cloneSource = QImage(m_document.background().size(), QImage::Format_ARGB32_Premultiplied);
    m_cloneSource.fill(Qt::transparent);
    QPainter painter(&m_cloneSource);
    SceneRenderer(m_document).render(painter, 1.0, /*showGuides*/ false);
    return m_cloneSource;
}

bool VpController::pickCloneSource(const QPointF &point)
{
    return m_cloneTool.pickSource(m_document.planes(), m_document.background().size(), point);
}

bool VpController::beginClone(const QPointF &point)
{
    if (!m_cloneTool.hasSource())
        return false;
    m_document.beginPaintTransaction();
    const QRect dirty = m_cloneTool.begin(m_document.paintLayer(), cloneSource(),
                                          m_document.planes(), m_document.background().size(), point);
    if (!m_cloneTool.drawing())
        return false;
    m_document.addPaintDirty(dirty);
    return true;
}

void VpController::moveClone(const QPointF &point)
{
    if (m_cloneTool.drawing())
        m_document.addPaintDirty(m_cloneTool.move(m_document.paintLayer(), point));
}

void VpController::endClone()
{
    if (!m_cloneTool.drawing())
        return;
    m_cloneTool.end();
    m_document.commitHistory();
}

void VpController::hoverClone(const QPointF &point)
{
    m_cloneTool.hover(m_document.planes(), m_document.background().size(), point);
}

void VpController::renderClonePreview(QPainter &painter, const QPointF &point)
{
    m_cloneTool.renderPreview(painter, cloneSource(), m_document.planes(),
                              m_document.background().size(), point);
}

void VpController::finishActiveStrokes()
{
    endBrush();
    endClone();
}
