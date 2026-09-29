#pragma once

#include <QObject>

namespace Tools
{
Q_NAMESPACE

// 数值也用于工具可用性位掩码，保持稳定。
enum Tool {
    CreatePlane = 0,
    EditPlane = 1,
    Marquee = 2,
    CloneStamp = 3,
    Brush = 4,
    Transform = 5,
    Hand = 6,
    Zoom = 7
};
Q_ENUM_NS(Tool)
}
