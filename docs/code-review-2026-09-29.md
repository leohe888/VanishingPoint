# 代码审查报告（VanishingPoint）

审查范围：`src/`（3,900 行）+ `tests/`（2,300 行），逐文件通读。
方式：静态阅读 + 符号引用扫描（未能编译运行，标注「已确认」= 读代码即可判定；「疑似」= 需运行复现）。

结论排序：**3 个确定性 bug → 1 处模型自相矛盾 → 一批可删死代码 → 性能热区 → 重复代码 → 职责边界**。

---

## 一、P0：确定性 bug / 明确错误

### 1. 重做快捷键执行的是撤销（已确认）
`src/qml/main.qml:62-65`

```qml
Shortcut {
    sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]
    onActivated: workArea.canvas.undo()      // ← 应为 redo()
}
```
`Ctrl+Y` / `Ctrl+Shift+Z` 与 `Ctrl+Z` 行为完全相同，重做在 UI 上完全不可达（`redo()` 只能在代码里调）。
验证：跑起来画一笔，Ctrl+Z 两次，再按 Ctrl+Y，看不到任何重做。

### 2. 边锁定的判定有两套互斥实现（已确认）
- 模型侧明确禁用几何判定：`src/core/vpdocument.cpp:121-125` 注释写着「**必须依赖 parentPlaneIndex / parentEdgeIndex 这条显式关系，不能靠"两端点几何重合"来判断**……几何匹配会失效」。
- 渲染侧仍在用几何判定：`src/core/scenerenderer.cpp:201-217`，用 `QLineF(a,oa).length() < 0.01` 逐边比对四个角点，命中就隐藏控制点。

两套规则必然漂移：子平面被缩放后共用边端点不再与父平面重合（这正是 vpdocument 注释里描述的场景），渲染器算不出「不可用」，于是本该隐藏的 3 个控制点又画出来了，用户拖一下就破坏共享边。
正确做法：`SceneRenderer` 只读 `PerspectivePlane::lockedEdgeMask()`，删掉 201-217 的几何比对。

### 3. 浮动图像拖动可能凭空产生一格历史（已确认）
`src/canvas/vpcanvas.cpp:759-761`

```cpp
} else if (const int plane = topmostPlaneIndexAt(m_doc.planes(), point); plane >= 0) {
    attachFloatingImageToPlane(m_draggedFloatingImageIndex, plane, point);
    m_floatingImageChanged = true;      // ← 无条件置位
}
```
`attachFloatingImageToPlane`（`vpcanvas.cpp:819-820`）在 `mapInverse` 失败时**静默 return**，什么也没改，但这里照样把 `m_floatingImageChanged` 置 true，松手时 `commitEdit(true)` 多记一格历史——撤销一次界面没变化。

---

## 二、P1：隐患（不改也可能出问题）

| # | 位置 | 问题 |
|---|---|---|
| 1 | `src/core/vpdocument.cpp:139` + `src/tools/../perspectiveplane.cpp:146-152` | 删除父平面时对子平面调 `clearParent()`：把 `m_angleToParentDegrees` 归 90°、清 `hasCustomAngle`，**但四边形几何保持旋转后的样子**。此后「记录夹角」与实际几何永久不一致，而 `rotatePlaneAroundEdge` 是拿记录夹角算旋转增量的（`perspectiveplane.cpp:579`）。 |
| 2 | `src/canvas/vpcanvas.cpp:44` | 硬编码 `C:\Users\yixin\Pictures\3.jpg` 作为默认背景。换机器必然加载失败（且泄露本机用户名）；加载失败后 `m_paintLayer` 为 null，`hasPaintContent()`/`brush` 路径全部走空。应放 qrc 或改为可配置/留空。 |
| 3 | `src/qml/ToolButton.qml:49-52` | `Shortcut { sequence: "V"/"C"/"M"/"S"/"B"/"T"/"H"/"Z" }` 是 WindowShortcut，**在 TextInput 里打字也会触发**。用户在「网格大小 / 角度」输入框里输入字母会切工具。需要 `enabled: !input.activeFocus` 或改用画布级快捷键。 |
| 4 | `src/canvas/vpcanvas.cpp:200/217` vs `vpcontroller.cpp:194/249` | `beginBrush`/`beginClone` 先 `beginPaintTransaction()` 再判断工具是否真的开始落笔；工具失败时事务挂起不清理（`m_paintBefore` 一直持有一份浅拷贝）。语义脏，容易被后续 `commitHistory()` 意外收编。 |
| 5 | `src/canvas/vpcanvas.cpp:850-866` 调用链 | `bakeSelectedFloatingImage()` → `removeFloatingImage()` → `setSelectedFloatingImage(-1)` 同步发 `imageSelectionChanged` → `VpCanvas` 里立刻 `setTool(EditPlane)` → `cancelInteraction()` → 可能 `m_doc.cancelEdit()`。**当前路径下 `m_editActive` 恰好为 false，所以没炸**；但「删除图像会同步触发挥销基线回滚」这个耦合是真陷阱，任何一个新的调用点都会踩。 |
| 6 | `src/core/vpdocument.cpp:154-165` | `addFloatingImage()` 不校验 `image.isNull()`，而同族的 `addFloatingImageOnSurface()`（167 行）校验了。不一致，靠调用方自觉。 |
| 7 | `src/core/perspectivequad.cpp:83-101` | `isValid()` 里 `8.0`（最短边）、`4.0`（最小叉积）、`100.0`（最小面积）是硬编码画布坐标。这套阈值假设背景约 1000px；换成 200px 的小图，合理平面会被判非法。 |
| 8 | `src/canvas/vpcanvas.cpp:529-535` | `stepAt` 里 `for (int i = 1; i < 15; ++i)` / `qBound(0, …, 14)` 手写数组长度，与 `levels[]` 的 15 个元素各写一遍；同一张缩放档位表在 `ZoomControls.qml:10-22` 又抄了一份。加档位必漏改。 |
| 9 | `src/canvas/vpcanvas.cpp:413-412`（注释） | `// 删除键按状态分派：…` 这段注释挂在 `VpCanvas::undo()` 头上，实际描述的是 `keyPressEvent`。注释与代码错位。 |

---

## 三、可删除（生产代码从未使用）

| 符号 | 位置 | 引用来源 | 说明 |
|---|---|---|---|
| `VpDocument::clearPainting()` | `vpdocument.cpp:60` | 无（连测试都没有） | 注释写「供"清除绘画"按钮判断」，但项目里没有这个按钮 |
| `VpDocument::editActive()` | `vpdocument.h:77` | 无 | — |
| `VpController::planeAngleLockReason()` | `vpcontroller.cpp:148` | 无（QML 未读） | 暴露成 Q_PROPERTY 却没人绑定，UI 从不显示锁定原因 |
| `canUndoChanged` / `canRedoChanged` / `documentAvailabilityChanged` | `vpdocument.h:81-83` | 无连接 | 三处 emit 全部落空，撤销/重做按钮的可用性根本没人接。要么接上要么删 |
| `canUndo()` / `canRedo()` | `vpdocument.h:69-70` | 仅测试 | 生产代码零调用 |
| `PerspectiveQuad::edgeIndexAt()` | `perspectivequad.cpp:144` | 仅测试 | 生产代码零调用（`resizePlaneFromEdge` 自己算交点） |
| `PerspectivePlane::isEdgeLocked()` | `perspectiveplane.cpp:123` | 仅测试 | 生产代码只用 `lockedEdgeMask()` 和 `setEdgeLocked()` |
| `FloatingImageTransformTool::mode()` | `floatingimagetransformtool.h:18` | 无 | `m_mode` 有别的读法（`isTransforming()`），这个 accessor 是纯冗余 |
| `FloatingImageTransformTool::update()` 的 Move 分支 | `floatingimagetransformtool.cpp:142-145` | 不可达 | 唯一调用点 `vpcanvas.cpp:733` 用 `isTransforming()`（Scale‖Rotate）做前置判断，`Mode::Move` 永远进不来 |
| `hoveredPlane` 参数链路 | `scenerenderer.h:21` → `drawPlaneGuides` | 不可达 | `vpcanvas.cpp:81` 恒传 `-1`，悬停高亮分支（`scenerenderer.cpp:149`）永不生效。要么实现悬停要么删参数 |
| `NavigationScrollBar` 的 `downlist.png` | `assets/icons/` | 未引用 | `res.qrc` 与所有 QML 都没引用；`ZoomControls` 用文本 `▴` 代替 |

> 注：`edgeIndexAt` / `isEdgeLocked` / `hasPaintContent` 有测试覆盖。删它们要同时删测试，或者承认「只为测试存在的生产 API」并在注释里说明——**目前的写法是两不管**。

---

## 四、性能热点（不是错，但改起来收益明确）

1. **`CloneTool::renderDab` 内层循环**（`clonetool.cpp:206-221`）：每个像素做 2 次 `QTransform::map`、1 次 `QLineF(...).length()`（开根）、4 次 `QImage::pixel()`（带格式分支的函数调用）。应改为：`constScanLine` + 指针索引、`distance²` 与 `radius²` 比较、循环外把 `canvasToTarget`/`sourceToCanvas` 展成 6 个系数手算。
2. **`CloneTool::applyDab` 每次分配 QImage + QPainter**（`clonetool.cpp:230-235`）。`move()` 一次最多插值 10000 个 dab（第 115 行），即最坏一次鼠标移动 10000 次图像分配 + 1 万次绘制到绘画层。应复用一个成员 dab 缓冲，并且**先在 dab 缓冲里合并整段笔触再一次性贴到 layer**。
3. **`MarqueeTool::copy()` / `clone()` 各分配整幅提取图**（`marqueetool.cpp:271/315`），且贴图上限 8192²。大选区一次 Alt 拖动就是几百 MB 峰值。
4. **`VpCanvas::selectionSampleImage()`**（`vpcanvas.cpp:891-899`）每次按下都重新合成整幅画布（背景+绘画层+所有浮动图像），且**不缓存**；而 `VpController::cloneSource()` 做的是同一件事却带缓存键（`vpcontroller.cpp:222-238`）。两套重复机制，应合并。
5. **拖动时逐帧深拷贝曲面快照**：`updateFloatingImageInteraction` → `attachFloatingImageToPlane`（`vpcanvas.cpp:800-824`）在每个 mouseMove 上把整个 surfaceGroup 的 quad 列表重新拷一遍并重新 `attachFloatingImage`。同一个 group 内连续移动时这份快照是常量，应该只在换组时重建。
6. `BrushTool::applyDab`（`brushtool.cpp:79` 与 `108`）：`bounds` 和 `uvBounds` 是**完全相同的表达式**，算两遍；83-85 行的循环只为了探测可映射性，`mapped` 结果被丢弃。
7. `PerspectiveQuad::isValid()` 在每次 mouseMove 里被调用 2 次（`PlaneEditTool::update` 之后 `setPlane` 再调一次），每次都做一遍 `quadToQuad` + 4 角 w 检查。`VpDocument::setPlane` 可以信任调用方已校验。

---

## 五、啰嗦 / 重复，可精简

| 位置 | 问题 | 建议 |
|---|---|---|
| `vpcontroller.cpp:46-101` | 6 个 setter 是同一个模板：取旧值 → set → 比较 → emit | 收敛成模板函数或宏；至少 6 处 → 1 处 |
| `brushtool.h:14-17` / `clonetool.h:14-19` | 两个工具各抄一遍 `setDiameter/setHardness/setOpacity` 的 `qBound` | 抽一个 `BrushParams` 值类型，两个工具各持一个 |
| `marqueetool.cpp:263-302` vs `306-362` | `copy()` 与 `clone()` 约 60 行几乎逐行同构，只差「映射方向 / clip 顺序 / 要不要减 `m_selectionFillOffset`」 | 合成一个私有函数 + 两个参数（offset、targetFromSource 标志） |
| `vpcanvas.cpp:119-259` | `mousePressEvent` 单函数 140 行 switch，含平面选中/挤出/旋转/复制/填充/取样六种决策 | 按工具拆 `pressForEditPlane() / pressForMarquee() / …`，switch 只做分派 |
| `vpcontroller.cpp:130-160` | `canSetSelectedPlaneAngle()` / `planeAngleEditable()` / `planeAngleLockReason()` 把**同一套规则写了三遍**（选中？有父？父已有自定义夹角的子？） | 保留一个返回 `{可编辑, 原因}` 的函数，另两个转调 |
| `vpdocument.cpp` 全篇 | 几乎每行都有注释，且大量是复述代码（「检查输出指针，避免向空指针写入结果」对 `if (!quad) return false;`） | 与项目既有约定（短注释、只说 why）冲突；建议删到 1/3 |
| `perspectiveplane.h:51-175` | 6 个自由函数的 Doxygen 注释合计 ~120 行，占文件 68%；参数已由签名表达 | 保留 `@param` 中真正有陷阱的（如「不是旋转增量」），其余删 |

---

## 六、过度设计

1. **`SceneRenderer::render()` 9 个参数、6 个默认值**（`scenerenderer.h:17-22`）。4 个调用点各传不同子集，靠位置和 `/*showGuides*/` 行内注释维持可读性（`vpcanvas.cpp:77-83`）。`drawContent` 这个开关的存在本身就说明渲染器混了两件事。建议拆成 `renderContent(painter)` + `renderOverlay(painter, options)`。
2. **`VpDocument` 同时维护两套事务系统**：`beginPaintTransaction/addPaintDirty/commitHistory`（绘画层脏矩形）与 `beginEdit/commitEdit/cancelEdit`（结构 COW 快照）。两套状态在 `commitEdit`/`cancelEdit`/`resetHistory` 里手工交叉复位（`vpdocument.cpp:331-361`）。上面 P1-4 的隐患正是这套交叉状态的产物。
3. **「边不可编辑」有三个真相来源**：`PerspectivePlane::m_lockedEdgeMask`、`parentPlaneIndex/parentEdgeIndex`、`SceneRenderer` 的几何比对。任何一处单独改都会不一致（见 P0-2）。
4. **浮动图像曲面快照的重复拷贝**：`FloatingImage::surfaceQuads`、`MarqueeTool::m_selectionFaces`、`VpCanvas::attachFloatingImageToPlane` 里临时 `surfaceQuads`——同一份几何在三处被深拷贝并各自演进。
5. **`ZoomControls.zoomItems` 用一个 `scale` 字段兼任三种语义**（正数=缩放比、0=分隔线、-1/-2=命令），再加两条重复项（`100%` 与「实际像素」同 `scale:1`）。应拆成 `{type, …}`。
6. **`NavigationScrollBar`** 手搓两个箭头 Rectangle + `Templates.ScrollBar` + `moveBy` 实现，而非直接用 `Controls.ScrollBar` 定制 `contentItem`。可以接受，但 `stepSize`/`arrowExtent`/`horizontal` 三个派生量让这个 69 行组件显得比它解决的问题更复杂。

---

## 七、职责边界与耦合

`docs/architecture.md` 自己承认 `VpCanvas` 通过 `m_doc` 越权写模型是「迁移期间的接口」。实际状态比文档更散：

- **`VpCanvas`（938 行）承担了 5 类职责**：事件分派、平面创建/编辑/挤出/旋转、浮动图像命中-拖动-吸附-烘焙、选区与取样协调、视口变换（缩放/平移/滚动/导航条量化）。建议至少拆出 **`CanvasViewport`**（`m_scale/m_offset/m_fitView/m_fillView/zoomAt/stepAt/scrollTo/*Position/*Size`，全部与文档无关）和 **`FloatingImageInteraction`**（`beginFloatingImageInteraction/update/end/bake` + 3 个成员）。
- **`VpController` 与 `VpCanvas` 的写入权不一致**：角度滑杆（`vpcontroller.cpp:162-177`）和粘贴（`:179`）由控制器改文档，而平面几何、浮动图像、烘焙由画布直接改文档。同一个模型被两个方向写，`beginEdit/commitEdit` 的配对因此散落两处。要么全收进控制器，要么把控制器降级为纯参数存储。
- **视图直接光栅化进模型**：`bakeSelectedFloatingImage`（`vpcanvas.cpp:850-872`）在画布里 `QPainter painter(&m_doc.paintLayer())` 并手工算脏矩形。这是文档行为，应成为 `VpDocument::bakeFloatingImage(index)`。
- **`SceneRenderer` 混内容渲染与编辑器叠加层**：背景/绘画层/浮动图像 vs 外框/网格/控制点/蚂蚁线。`drawContent` 开关是这一混合的补丁。网格与控制点属于「编辑器 UI」，不属于场景渲染。
- **`CloneTool` 混三件事**：源取样与偏移语义（模型）、逐像素单应重采样（渲染）、光标预览位图合成（UI）。`renderPreview` 里出现的 `QImage dab` + `painter.drawImage` 与 `applyDab` 是同一份像素逻辑的两条路径，长期会漂移（现在已经各写一遍 `dabRect` 调用顺序）。
- **`MarqueeTool` 同理**：选区几何（模型）+ 三套单应拼贴光栅化（渲染）。

---

## 八、建议的最小修复顺序

1. `main.qml:63` → `redo()`。**1 行，先修。**
2. `scenerenderer.cpp:201-217` 删掉几何比对，改用 `lockedEdgeMask()`。**消除模型自相矛盾。**
3. `vpcanvas.cpp:761` 按 `attachFloatingImageToPlane` 的返回值决定是否置 `m_floatingImageChanged`（顺带把该函数改成返回 bool）。
4. `vpcanvas.cpp:44` 换掉硬编码路径。
5. 删第三节的死代码（`clearPainting`/`editActive`/`planeAngleLockReason`/`mode()`/Move 分支/`hoveredPlane`），顺手决定三个空 emit 信号是接上还是删掉。
6. 性能：先把 `CloneTool::applyDab` 改成复用 dab 缓冲 + 合并整段笔触再贴一次；再把 `renderDab` 内层换成 scanline。

> 上述都未改动任何代码。需要我动手的话，建议按 1→2→3 一次一条，避免把重构和修复打包。
