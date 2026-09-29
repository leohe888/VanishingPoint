#pragma once

#include "core/perspectiveplane.h"

class PlaneEditTool
{
public:
    void begin(const PerspectivePlane &plane, const QPointF &pressPoint, int controlPointIndex,
               int edgeIndex, bool extrude, const QSize &canvasSize,
               bool rotate = false, int rotationEdgeIndex = -1);
    bool update(const QPointF &point, PerspectivePlane *result);
    bool extruding() const { return m_mode == Mode::Extrude; }
    bool rotating() const { return m_mode == Mode::Rotate; }
    int edgeIndex() const { return m_edgeIndex; }

private:
    PerspectivePlane m_initialPlane;
    QPointF m_pressPoint;
    int m_controlPointIndex = -1;
    int m_edgeIndex = -1;
    enum class Mode { Move, Corner, Edge, Extrude, Rotate };
    Mode m_mode = Mode::Move;
    int m_rotationEdgeIndex = -1;
    qreal m_lastPointerAngle = 0.0;
    qreal m_accumulatedRotation = 0.0;
    QSize m_canvasSize;
};
