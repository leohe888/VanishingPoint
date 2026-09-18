# Facet 是什么

`Facet` 是 VanishingPoint 中表示“四边形映射面”的基础几何结构，定义在
`src/core/perspectiveplane.h` 中：

```cpp
struct Facet {
    QPointF corner[4];
    QPointF surfaceCorner[4];
};
```

它保存两组一一对应的四个角点，分别描述同一个面在图片坐标空间和展开曲面坐标空间中的形状。

## `corner[4]`：图片坐标

`corner` 表示四边形在背景图片上的实际位置，也就是用户在画布上看到的透视平面：

```cpp
facet.corner[0] = QPointF(100, 100);
facet.corner[1] = QPointF(400, 130);
facet.corner[2] = QPointF(350, 400);
facet.corner[3] = QPointF(130, 350);
```

角点必须沿四边形边界依次排列：

```text
corner[0] ───── corner[1]
    ╲               ╲
     ╲               ╲
corner[3] ───── corner[2]
```

四边形可以有透视变形，但必须是非自交、非退化的凸四边形。

## `surfaceCorner[4]`：展开曲面坐标

`surfaceCorner` 表示同一个面展开后的二维形状。新建平面时通常将其初始化为规则矩形：

```cpp
facet.surfaceCorner[0] = QPointF(0, 0);
facet.surfaceCorner[1] = QPointF(300, 0);
facet.surfaceCorner[2] = QPointF(300, 250);
facet.surfaceCorner[3] = QPointF(0, 250);
```

```text
surfaceCorner[0] ─── surfaceCorner[1]
        │                    │
        │                    │
surfaceCorner[3] ─── surfaceCorner[2]
```

## 两组角点的对应关系

两组角点按索引一一对应：

| 图片坐标 | 展开曲面坐标 |
| --- | --- |
| `corner[0]` | `surfaceCorner[0]` |
| `corner[1]` | `surfaceCorner[1]` |
| `corner[2]` | `surfaceCorner[2]` |
| `corner[3]` | `surfaceCorner[3]` |

程序根据这四组对应点计算单应变换，从而在以下坐标空间之间转换：

```text
展开曲面上的矩形内容
          ↓ 单应变换
背景图片上的透视四边形
```

例如，画笔或浮动图片可以先在规则的展开曲面坐标中处理，再投影到背景图片中的透视平面。

## `Facet` 与 `Plane` 的关系

`Plane` 继承自 `Facet`：

```cpp
struct Plane : Facet {
    int surfaceGroup = -1;
    quint8 lockedEdges = 0;
    int parentPlane = -1;
    int parentEdge = -1;
    qreal relativeAngle = 90.0;
    bool angleAdjusted = false;
};
```

因此两者的职责不同：

- `Facet` 只描述四边形在两个坐标空间之间的几何映射。
- `Plane` 在 `Facet` 的基础上增加曲面分组、锁定边和父子平面关系等编辑状态。

可以将它们理解为：

```text
Facet
├── 图片中的四个角点
└── 展开曲面中的四个角点

Plane
├── Facet 的全部几何数据
├── 曲面分组
├── 锁定边
└── 父子平面关系
```

## 为什么 `FloatingImage` 保存 `Facet`

`FloatingImage` 中包含：

```cpp
QVector<Facet> faces;
```

图片吸附到透视曲面时，程序会复制当时的 `Facet` 几何作为快照。之后即使用户修改或删除原来的 `Plane`，浮动图片仍然保留自己的投影关系，不会因为原平面的变化而丢失或意外变形。

因此可以把 `Facet` 概括为：

> 一个不包含编辑状态、只描述透视映射关系的四边形几何数据或几何快照。
