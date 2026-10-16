#pragma once

#include "../core/floatingselection.h"

// State for one move, scale or rotation gesture. It does not own document history.
class FloatingSelectionTransformTool
{
public:
    enum class Mode { Idle, Move, Scale, Rotate };

    bool beginTransform(const FloatingSelection &image, const QPointF &canvasPoint,
                        int handleIndex, Mode mode);
    void beginMove(const FloatingSelection &image, const QPointF &placementOffset);
    bool update(const QPointF &canvasPoint, bool constrain, bool fromCenter,
                FloatingSelection *result) const;
    void reset() { m_mode = Mode::Idle; }

    bool isTransforming() const { return m_mode == Mode::Scale || m_mode == Mode::Rotate; }
    const FloatingSelection &startSelection() const { return m_startSelection; }
    QPointF grabOffset() const { return m_grabOffset; }

    static int handleAt(const FloatingSelection &image, const QPointF &canvasPoint, qreal viewScale);
    static int rotationCornerAt(const FloatingSelection &image, const QPointF &canvasPoint,
                                qreal viewScale);

private:
    Mode m_mode = Mode::Idle;
    FloatingSelection m_startSelection;
    int m_activeHandle = -1;
    int m_activeQuadIndex = -1;
    QPointF m_grabOffset;
    QPointF m_pressPosition;
};
