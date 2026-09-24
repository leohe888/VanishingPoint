#pragma once

#include "floatingimage.h"

// State for one move, scale or rotation gesture. It does not own document history.
class FloatingImageTransformTool
{
public:
    enum class Mode { Idle, Move, Scale, Rotate };

    bool beginTransform(const FloatingImage &image, const QPointF &canvasPoint,
                        int handleIndex, Mode mode);
    void beginMove(const FloatingImage &image, const QPointF &placementOffset);
    bool update(const QPointF &canvasPoint, bool constrain, bool fromCenter,
                FloatingImage *result) const;
    void reset() { m_mode = Mode::Idle; }

    Mode mode() const { return m_mode; }
    bool isTransforming() const { return m_mode == Mode::Scale || m_mode == Mode::Rotate; }
    const FloatingImage &startImage() const { return m_startImage; }
    QPointF grabOffset() const { return m_grabOffset; }

    static int handleAt(const FloatingImage &image, const QPointF &canvasPoint, qreal viewScale);
    static int rotationCornerAt(const FloatingImage &image, const QPointF &canvasPoint,
                                qreal viewScale);

private:
    Mode m_mode = Mode::Idle;
    FloatingImage m_startImage;
    int m_activeHandle = -1;
    int m_activeQuadIndex = -1;
    QPointF m_grabOffset;
    QPointF m_pressPosition;
};
