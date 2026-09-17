#pragma once

#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>
#include "projectivemapping.h"

// 图片上一个带透视效果的四边形
struct Facet {
    QPointF corner[4];  // 四个角点在画布上坐标，依次为左上 / 右上 / 右下 / 左下
    QPointF surfaceCorner[4];   // 四个角点在展开曲面上的坐标
};

struct Plane : Facet {
    int surfaceGroup = -1;  // 所属的展开曲面分组（共享曲面的相邻平面同组）
    quint8 lockedEdges = 0; // 与相邻垂直平面共用、不可编辑的边（位掩码）
    int parentPlane = -1;   // 由哪个平面拖出；仅子平面可设置夹角
    int parentEdge = -1;    // 父平面上对应的共享边
    qreal relativeAngle = 90.0; // 与父平面的夹角（度）
    bool angleAdjusted = false; // 用户是否手动调整过夹角
};

// 平面几何与透视构造的纯函数集合：不持有状态，上下文一律由调用方传参，便于独立测试。
namespace PlaneMath {
ProjectiveMapping surfaceMapping(const Facet &facet);
ProjectiveMapping uvMapping(const Facet &facet);

constexpr qreal Epsilon = 1e-6; // 浮点比较用的极小量

// —— 基础几何 ——
QPolygonF planePolygon(const QPointF corner[4]);   // 把 4 个角点组装为多边形
QVector<QPointF> handles(const Facet &facet);      // 面片的 4 个角点 + 4 个边中点
// 计算点 p 到线段 ab 的距离；t 返回最近点在线段上的参数化位置（0~1）
qreal distanceToSegment(const QPointF &p, const QPointF &a,
                        const QPointF &b, qreal *t = nullptr);
bool isValidPlane(const Facet &facet);             // 校验是否为有效的凸四边形

// —— 坐标变换（单应） ——
// 接受 Facet，因此对平面和浮动图像的几何快照都适用。
QPointF uvToPlane(const Facet &facet, const QPointF &uv);             // 归一化 UV -> 图像坐标
QPointF planeToUv(const Facet &facet, const QPointF &point,           // 图像坐标 -> 归一化 UV
                  bool *ok = nullptr);
QPointF planeToSurface(const Facet &facet, const QPointF &point,      // 图像坐标 -> 展开曲面坐标
                       bool *ok = nullptr);

// —— 命中测试（tolerance 为图像坐标系下的拾取半径） ——
int planeAt(const QVector<Plane> &planes, const QPointF &point);        // 点所在的最上层平面
int handleAt(const Facet &facet, const QPointF &point, qreal tolerance); // 控制点索引
int edgeAt(const Facet &facet, const QPointF &point, qreal tolerance);   // 边缘索引

// —— 平面构造算法（pressPoint 为本次拖动的按下起点） ——
// 平移四边形并同步移动展开坐标；越过地平线或退化时返回 false，调用方保留最后有效位置。
bool movePlaneOnSurface(const Plane &source, const QPointF &dragPoint,
                        const QPointF &pressPoint, Plane *result);
// 沿某条边方向缩放平面：只改变该边到对边的距离，保持透视关系不变
Plane resizePlaneAlongEdge(const Plane &source, int edge,
                           const QPointF &dragPoint, const QPointF &pressPoint);
// 由平面法线恢复投影后的第三个消失方向（backgroundSize 用于估计焦距）
bool perpendicularDirection(const Plane &source, const QPointF &atPoint,
                            const QSize &backgroundSize, QPointF *direction);
// 从源平面的一条边拖出与之垂直的新平面（Ctrl+拖动边缘）
Plane makePerpendicularPlane(const Plane &source, int edge,
                             const QPointF &dragPoint, const QPointF &pressPoint,
                             const QSize &backgroundSize);
// 绕共享边三维旋转子平面再重投影；0°/180° 时与父平面严格共面（backgroundSize 估焦距）。
Plane rotateChildPlane(const Plane &source, int edge, qreal targetAngle,
                       const QSize &backgroundSize);

} // namespace PlaneMath
