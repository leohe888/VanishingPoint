# 全项目代码审核（2026-09-29）

## 范围与验证

以当前工作区为准，包含用户已有的未提交修改。逐文件阅读 `src`、`tests`、根目录 CMakeLists.txt 和 res.qrc，共 56 个文本代码/构建文件、9078 行（包含注释与空行）；另阅读 AGENTS.md 和 docs/architecture.md。图片资源只检查引用，不将二进制文件算作代码。生成的 build 文件不纳入源码审核。

没有修改产品源码或现有测试。新增本报告；独立复现程序和执行脚本放在被忽略的 build 目录。

- Qt 6.11.2 / MinGW Debug 构建成功。
- CTest：12/12 个测试程序通过，运行约 21.7 秒。这表示已有断言通过，不代表所有交互组合正确。
- 独立复现：`build/audit-probe.cpp`，执行脚本 `build/audit-run.ps1`。在项目根目录执行脚本；依赖已构建的 tst_canvasnavigation 对象文件。最后的 delete 案例会在独立子进程中故意触发现有越界问题。
- 下文标明“已复现”的问题有运行结果；“静态确认”依据代码路径；数值与大图性能风险未进行长时间压力测试。

P1：崩溃、撤销数据不完整或破坏共享几何，应优先修复。P2：确定的功能错误或有明确触发条件的风险。P3：精简、接口收敛与维护性改进。

## 缺陷和隐患

### B01 / P1：拖动浮动图像时删除，会继续使用失效索引【已复现】

位置：`src/canvas/vpcanvas.cpp:447`、`:762`；`src/core/vpdocument.h:51`。

鼠标按住图像，按 Delete，再移动鼠标。删除分支没有结束图像交互；`m_draggedFloatingImageIndex` 仍保留旧下标，而后续路径调用无边界保护的 `floatingImage()`。独立案例只有一张图像，删除后数量为 0，继续移动触发 `QList::at: index out of range`，进程退出码 -1073740286。如果还有其他图像，下标错位也可能修改另一个对象。Transform 工具有时会因选择消失而切换工具、顺便取消交互，但 EditPlane 等工具没有这个保护。

建议：删除前统一取消/结束当前交互，然后重新获取选择；事件入口同时检查目标是否仍存在。当前规模下，先正确管理索引生命周期即可，无须立即引入复杂实体管理框架。

### B02 / P1：笔触期间粘贴，切断绘画事务，导致撤销遗留像素【已复现】

位置：`src/canvas/vpcontroller.cpp:179`、`:186`；`src/core/vpdocument.cpp:163`、`:374`。

窗口级 Ctrl+V 可以在鼠标落笔期间触发。`pasteImage()` 直接追加图像并提交历史；这次提交消耗、清空正在进行的绘画事务，但 BrushTool/CloneTool 仍处于 drawing 状态。随后续笔没有新的 before 快照，笔触结束提交的历史不含后续像素增量。

复现：起笔 (50,50) → 粘贴 → 续笔到 (80,50) → 抬笔 → 撤销两次。所有记录已回退，但 `hasPaintContent()` 仍为 true，(80,50) 的 alpha 为 255。

建议：粘贴作为独立文档命令，执行前统一收尾当前交互。既要处理笔触，也要处理平面/浮动图像/选区的预览事务；只在 pasteImage 内调用 endBrush 不能解决全部重入组合。

### B03 / P1：共享边锁定只影响显示，编辑仍可修改锁定点【已复现】

位置：`src/canvas/vpcanvas.cpp:144`；`src/tools/planeedittool.cpp:55`；`src/core/scenerenderer.cpp:195`。

渲染器按 lockedEdgeMask 隐藏控制点，但命中测试检查全部八个点，PlaneEditTool 也不检查锁。独立案例将第 0 条边锁定，仍把其角点从 (100,100) 拖到 (110,110)。实际父子面片会因此撕开共享边。

建议：共用一份“允许哪些编辑动作”的规则，供显示、命中和执行查询。不能简单禁止所有与锁边相邻的边缩放：应明确缩放是否允许改变共享端点，以及如何同步关联面片。Ctrl 挤出也应检查边是否已有连接，避免同一条边重复生成子面，删除其中一个子面又无条件解锁仍被使用的父边。

### B04 / P2：重做快捷键调用了撤销【静态确认】

位置：`src/qml/main.qml:62`，尤其第 64 行。

Ctrl+Y、Ctrl+Shift+Z 都调用 `workArea.canvas.undo()`，应调用 `redo()`。C++ 的 redo 单元测试通过，无法发现 QML 接线错误。

### B05 / P2：沿吸附快照外推移动成功，但没有记录历史【已复现】

位置：`src/canvas/vpcanvas.cpp:762`、`:778`、`:828`。

非 Transform 工具下，光标未命中当前文档平面，但 `moveSurfaceAttachedImage()` 成功时，会更新 origin，却没有设置 `m_floatingImageChanged`。随后 `commitEdit(false)` 丢掉事务，修改保留在文档中，但没有撤销记录。

复现：吸附图像处于宿主外推区域，origin 从 (250,100) 移到 (260,100)，`canUndo()` 为 false。

建议：成功计算候选后统一比较起始与最终状态，避免在各个分支中维护互相不一致的 changed 布尔值。

### B06 / P2：烘焙选中图像改变了原有遮挡顺序【已复现】

位置：`src/canvas/vpcanvas.cpp:849`；`src/core/scenerenderer.cpp:47`。

场景总是先绘画层、再所有浮动图像。把上层浮动图像烘焙到绘画层后，它会落到其他浮动图像下面。复现：下层红图、上层蓝图重叠；点击空白烘焙蓝图，中心像素从蓝色变为红色，与“烘焙无像素跳变”的注释不符。

建议：明确合并语义。简单方案是按层序一起烘焙选中图像及其下方图像，并保留上方图像；若必须仅合并一个对象并保留所有其他对象可编辑，则需要能表达插入位置的图层模型。这个决策会影响产品行为，应先明确再实施。

### B07 / P2：无改动操作占用历史并删除重做分支【已复现】

位置：`src/canvas/vpcanvas.cpp:340`；`src/canvas/vpcontroller.cpp:167`；`src/core/vpdocument.cpp:365`。

普通平面编辑无条件 `changed = true`，点一下、没有 mouseMove，也提交历史。独立案例角点完全没变，但 canUndo 为 true。撤销后点一下平面，会进入 commitHistory 并删除原重做分支。角度修改也有类似问题：旋转失败时返回 source，但 setPlane 只验证有效性，仍返回 true 并提交。

建议：比较实际持久化状态，没变就不提交；成功处理请求与实际数据变更应分开表达。

### B08 / P2：鼠标松开位置没有统一参与最终计算【静态确认】

位置：`src/canvas/vpcanvas.cpp:327`、`:332`、`:348`、`:354`。

松开时仅缩放/旋转图像和选区更新最终位置；普通图像平移、平面编辑/挤出、画笔、图章都依赖最后一次 mouseMove。如果松开位置不同于最后一次移动位置，就丢失末段位移/笔触；挤出可能仍提交旧预览。

建议：在 release 中执行一次对应的最终 update，然后结束操作。防止重复落笔应由工具忽略零距离更新解决；BrushTool 当前即使 distance 为 0 也会补一个 dab，需要一并处理。

### B09 / P2：Alt 拖动绕过控制器的角度锁定规则【静态确认】

位置：`src/canvas/vpcanvas.cpp:177`；`src/canvas/vpcontroller.cpp:130`。

控制器在选中面具有调整过夹角的子面时禁止 setPlaneAngle，QML 输入框也不可用。但 Canvas 的 Alt+对边中点拖动只检查选中面有父面，不检查同一锁定条件，直接更新几何。当父/子/孙形成链时，可绕过界面限制并改变共享曲面解释。

建议：角度编辑只有一个命令入口；输入框和鼠标拖动查询、执行同一规则。链上的其他面是否跟随变换，也须在该命令中明确。

### B10 / P2：透视映射返回 true，不表示映射点安全【静态确认；性能后果未压力复现】

位置：`src/core/perspectivetransform.cpp:12`、`:20`；`src/tools/brushtool.cpp:83`、`:117`。

mapForward/mapInverse 仅检查变换对象与出参，然后委托 QTransform。当前测试甚至明确要求 NaN、Inf、地平线附近输入仍返回 true。BrushTool/MarqueeTool 等调用方却把这个 bool 当作有效坐标的保证。BrushTool 的四角“验证”只会再次得到 true，mapped 值没被检查；补点数量未限制，巨大展开距离可能带来长循环或浮点转整数越界。

建议：明确原始映射和安全映射的契约。安全路径检查输入/输出有限性与齐次分母，笔触域还需检查整个 dab 是否跨越极点。是否允许负分母/另一支外推，应按操作语义决定，不能一刀切禁止所有平面外坐标。画笔设置补点预算，图章已有 count 上限可作为参考。修改契约时同时调整那些固化“不安全输入仍成功”的测试。

### B11 / P2：网格线数量没有上限，强透视或大平面可能卡住渲染【静态风险】

位置：`src/core/scenerenderer.cpp:171`。

网格线数量按画布边长/gridSize 决定，gridSize 最小 1。几何操作允许接近 1e7 的坐标，渲染器可以尝试逐帧绘制百万级线段；即使绝大部分不在视口，也会参与循环。蚂蚁线定时重绘会进一步放大成本。

建议：按屏幕可见间距、可见区域限制绘制密度，并设明确的线段数量预算。投影/网格可在几何或 gridSize 变化时更新，不应每次重绘重算所有内容。

### B12 / P2：选区导出超过 8192 时静默裁掉内容【静态确认】

位置：`src/tools/marqueetool.cpp:268`、`:313`。

copy/clone 将输出宽高截到 8192，但映射仍按原展开坐标尺寸计算，图像的 scaleFactors 仍为 (1,1)。这实际丢弃了超出边界的区域，而非按比例降采样；不是保留整块选区的内存限制策略。每个最大输出本身约 256 MiB，还未计取样图、预览层与历史。

建议：明确拒绝超限并报告原因，或缩小输出分辨率同时保留原 placement 尺寸。应先限制浮点尺寸再转换为整数，并检查分配失败。

### B13 / P2：启动图像硬编码且没有用户打开入口【静态确认】

位置：`src/canvas/vpcanvas.cpp:44`；`src/core/scenerenderer.cpp:41`；`src/qml/main.qml`。

启动依赖 `C:\Users\yixin\Pictures\3.jpg`，加载失败未提示。当前 UI 没有打开图像动作，文档 loadImage 也未通过控制器暴露。其他电脑上会没有背景、绘画层为空，即使粘贴也不能正常构成当前预期的文档。空文档提示还画在空 background 的 rect 中，通常不可见。

建议：控制器提供打开文档命令，文件选择器调用它；演示图片作为可选资源/开发配置。空文档提示在视图的有效边界中绘制。成功切换文档时统一结束交互、刷新导航并重置图章源状态；不能仅通知“文档可用”。

### B14 / P2：大有限角度可能使归约循环无法结束【API 数值边界风险】

位置：`src/core/perspectiveplane.cpp:579`；`src/canvas/vpcontroller.cpp:164`。

targetAngle 仅检查有限性，随后用反复 ±360 归约。非常大的有限 double 会执行大量循环；当 360 小于当前数值的有效精度，减法不改变 delta，循环永不结束。当前 QML 输入框限制在 0~360，正常输入不会触发，但公开 C++/属性 setter 接受更大值。

建议：用常数时间的 fmod/remainder 归约，保持现有 (-180,180] 与正整周记为 360 的语义；先归约各角度，避免两有限数相减溢出。增加大有限角度测试。

### B15 / P3：模型验证契约不一致，难以维护不变量【静态确认】

位置：`src/core/vpdocument.cpp:87`、`:103`、`:154`、`:234`、`:242`；`src/core/vpdocument.h:37`。

setFloatingImage 检查有限 origin、scale 和 rotation，但 origin/attach 的快捷写接口绕过检查；addFloatingImage 不拒绝空图；setSelectedPlane 不规范化下标。quad.isValid 只验证 canvas，测试也确认 surface 全零/NaN 时仍有效，所以它不能承担完整平面的合法性保证。

建议：在模型写入边界统一验证，而非在每个鼠标分支补防御；区分“画布四边形可编辑”与“完整面片可投影”。保留明确合法的外推场景，避免为了安全而改变现有坐标语义。

### B16 / P3：面积注释与实现/测试不一致【静态确认】

位置：`src/core/perspectivequad.cpp:98`；`tests/tst_perspectivequad.cpp:161` 附近。

twiceArea 是面积的两倍，比较 `<100` 只保证实际面积至少 50。注释与测试用例注释写的是面积 100，而测试实际按 twiceArea=100 验证通过。挤出入口又用包围盒面积判断，三个阈值语义不一致。

建议：确定产品最小面积要求，然后统一实际面积、阈值和测试名称；包围盒面积不能代替倾斜四边形面积。

## 可删除或收敛的代码

这里区分“仓库完全没有调用”与“产品当前没有接入”。公共 C++/QML 属性理论上可以被外部调用，因此结论限定当前项目。

| 项目 | 当前引用情况 | 建议 |
| --- | --- | --- |
| PlaneEditTool::initialPlane，`src/tools/planeedittool.h:12` | 声明之外无调用 | 可删除 getter；保留内部 m_initialPlane，它参与计算 |
| FloatingImageTransformTool::mode，`src/tools/floatingimagetransformtool.h:18` | 声明之外无调用 | 可删除 getter；Mode 与 m_mode 被实现使用，不能一起删 |
| PerspectiveQuad::edgeIndexAt 和 pointToSegmentDistance | 只有测试调用，产品只用控制点命中 | 若产品不支持沿整条边命中，可连同专属测试一起删；若计划支持，接入实际命中路径 |
| PerspectiveTransform::inverse getter | 只有测试直接查询，产品用 mapInverse | 若不作为公开几何接口，可删除 getter并让测试检验行为；内部 inverse 必须保留 |
| VpDocument::clearPainting | 无产品或测试调用 | 接入清除命令并测试，或删除当前未使用功能 |
| VpDocument::hasPaintContent | 产品未调用，仅测试使用 | 它是 O(像素数) 扫描；可保留测试查询，但注释中的清除按钮目前不存在，不要误接到逐帧 UI 查询 |
| canUndoChanged/canRedoChanged/documentAvailabilityChanged | 有 emit，无订阅 | 若近期不提供 UI 状态，可收敛；若要接入，应修正通知时机，避免 loadImage 尚未清理完状态就发信号 |
| planeAngleLockReason 属性/getter | QML 没有读取，仅定义/实现 | 优先接到不可编辑时的提示，复用锁定规则；否则删掉未使用的展示分支 |
| SceneRenderer::render 的 hoveredPlane、drawContent | 当前调用分别始终 -1、true | 删除无需求参数/分支。不要误删 showGuides，离屏内容渲染确实使用 false |
| `assets/icons/app.ico`、`assets/icons/downlist.png` | 没有资源/构建/UI 引用 | 可删除未接入资源，或正式接入窗口图标/下拉控件 |
| `src/tools/planecreatetool.h:6` 的 QString include | 该头没有使用 QString | 可删除 |
| Qt5/Android/macOS 模板 CMake 分支与 main 的旧高 DPI 分支 | 当前目标 Qt 6.11.2 Windows；源码使用 Qt6 API/新版 QML语法 | 若只支持声明环境，收敛为 Qt6 Windows。若要跨平台兼容，需要真正编译验证，当前条件分支不能证明兼容 |

不能删除：bitmapClip 被命中测试使用；m_editBefore/m_editPaintBefore 被取消事务使用；paintBefore/paintAfter 与脏矩形被撤销/重做使用；surfaceQuads 是内容独立于可编辑平面的快照。它们不是重复存储就必然冗余。

## 简化与优化

1. `src/canvas/vpcanvas.cpp:807`：逐角点重建 PerspectiveQuad 可直接 `surfaceQuads.append(plane.quad())`。当前 Quad 只有两组坐标，没有需要过滤的交互元数据。
2. `src/core/scenerenderer.cpp:198`：用循环从 lockedEdgeMask 重新复制一份掩码，可直接赋值。更关键的是后面的几何重合扫描与显式父子/锁信息并存，导致渲染与交互两套判断；修复 B03 后集中查询一次可用控制点。
3. `src/core/vpdocument.cpp:304`、`:323`、`:368`：结构快照的四个字段赋值出现三次；用一个小的 captureStructure 函数统一即可。事务清理的重复字段归零也可集中，避免后续添加成员时漏清理。无需为此引入命令类层级。
4. `src/canvas/vpcontroller.cpp:130`、`:148`：夹角可编辑性和原因重复遍历、判断。返回一个简单的限制原因/状态，enabled 与提示从同一结果派生；与 Alt 拖动共用。
5. `src/tools/brushtool.cpp:77`、`:108`：bounds 和 uvBounds 内容相同，可复用。四角映射循环当前没有真正验证结果，应按 B10 改成有效检查，不能当作纯冗余直接删掉安全意图。
6. `src/tools/brushtool.cpp:77`：一笔内 Quad 不变，每个 dab 重建 UV 变换并求逆没有必要；begin 时缓存。图章逐 dab 分配 QImage、创建 QPainter，细长笔迹成本较高，可复用工作缓冲区或一笔一个 painter；确保取样源快照与合成语义不改变。
7. `src/canvas/vpcontroller.cpp:224`：drawing 时仍序列化完整缓存键，随后才 return；可先处理 drawing 早退。当前预览还没有源点时也先合成 cloneSource，可在控制器入口先检查 hasSource。投影缓存键每次序列化全部面片，只有性能测量确认瓶颈后才改为修订号，避免缓存失效机制过度设计。
8. `src/tools/marqueetool.cpp:193`：Ctrl 预览每帧从 COW 全层快照恢复，再写入触发整层复制。copy、clone、fill 还有重复的面片裁剪/映射构造，可抽取一小段纯补丁计算；优先仅恢复/更新脏区，或渲染临时预览层。40 个历史状态只限制条目数，单条全图 before/after 仍可很大，后续按像素字节量限制历史更有效。
9. `src/qml/ToolOptionsBar.qml`：直径/硬度/不透明度的三项在画笔与图章行重复。可用一个小组件，通过明确 getter/setter 参数复用。不要用大量字符串动态拼接属性名，牺牲类型检查换取少量行数。
10. `src/qml/ToolBar.qml:32` 与 main 的 Connections：工具栏维护工具状态并再次发命令，控制器通知又调用同一 selectTool，形成反馈往返。直接绑定 currentTool 到 controller.tool，用户点击只发一次请求；拒绝变换时也能自然保持正确选中状态。
11. ZoomControls.qml 与 VpCanvas::stepAt 两处维护缩放档位；统一来源。C++ 的 15/14 魔数改为数组长度。缩放光标两种图标可以一次构造后复用，但收益较小，不优先于正确性。
12. SceneRenderer::render 有 11 个参数，调用依赖多组位置 bool。删掉未使用参数后，内容渲染与辅助层渲染分为两个明确入口，剩余显示参数用一个小值结构体；不需要虚拟渲染器/插件机制。
13. tests/CMakeLists.txt 对各测试重复编译相同几何与模型源码。一个内部静态库/对象库供程序和测试复用，可以减少全量编译工作，仍按功能划分独立测试。当前项目不需要动态插件或公共安装 SDK。
14. `perspectiveplane.h` 的长注释多次重复出参非空和失败保持原值；cpp 中也有逐句翻译代码的注释。保留坐标系、精度原因、相机假设、失败语义、外推限制；删除“检查输出指针”“列表为空返回 -1”等可以直接读代码得到的句子。另 projectedNormalDirection 的焦距公式注释少了实现中的负号，应纠正。

## 职责、高内聚与低耦合

| 类型 | 判断 | 推荐边界 |
| --- | --- | --- |
| VpCanvas | 当前问题最明显：938 行，既做导航/事件/绘制，又做平面命令、图像吸附烘焙、选区取样和历史协调 | 保留控件坐标转换、导航、事件转发、刷新与交互辅助显示；文档命令移到控制器 |
| VpController | 已脱离 QQuickItem，方向合理；但只拥有画笔/图章，其他工具在 Canvas，统一命令调度缺失 | 拥有所有工具状态，统一 begin/update/end/cancel 和粘贴/删除/历史命令；只接受图像坐标，不读取控件尺寸 |
| VpDocument | 保存数据和历史本身合理；问题在于公开可写 paintLayer、部分方法自动 commit、部分要求外部 commit，写入语义不一致 | 明确哪些是预览写入，哪些是完整命令；将事务实现留在模型，操作协调留在控制器；现阶段不必仅为“单职责”硬拆历史类 |
| PerspectivePlane / PerspectiveQuad | 拆分有实际用途：平面保存拓扑/编辑元数据，Quad 作为内容的几何快照 | 保留拆分；共边不变量应由文档/几何命令维护，Quad 不承担整个文档关系检查 |
| FloatingImage / FloatingImageProjection | 模型姿态与投影补丁分离合理；投影用于渲染、命中、控制点，缓存有复用价值 | 保留；若后续性能不足，统一修订/缓存失效，而非立刻取消投影对象 |
| SceneRenderer | 内容渲染、辅助显示、锁边规则混在一起；还保存上次 render 的 viewScale，renderFloatingImage 行为依赖调用历史 | 共边编辑权限移到统一规则，内容/辅助入口分开；缩放显式传入需要它的绘制入口，消除隐含状态 |
| 各 Tool | 大多数只计算几何/像素，不持有文档历史，内聚性较好 | 保留这种分工；PlaneEditTool 两个 bool 加多个索引表达模式容易出现非法组合，可用简单 enum 区分 Move/Corner/Edge/Extrude/Rotate |

不建议增加通用 Tool 基类、每个参数一个 Command 类、依赖注入容器或泛化状态机框架。目前 bug 来自同一次交互由两处协调、规则重复、事务入口不一致。把已有流程集中，再用少量明确状态表达模式，就能解决主要问题。docs/architecture.md 的迁移方向总体正确，但“Canvas 只暴露 controller”已与实际 zoom/navigation/history 接口不一致，应更新文档。

## 测试审核与补齐顺序

已有解析几何、真实相机场景、跨面片选区取样和笔触撤销测试有实际价值，不建议因行数多删除。PerspectivePlaneTest 中基本 Quad 校验/命中案例与专门的 PerspectiveQuadTest 重复，PerspectiveTransformTest 中手写的角点数量/退化案例与后来 data-driven 版本重复，可合并到职责归属明确的测试中。值类型默认拷贝的多组重复测试可精简为有代表性的快照独立性案例。

优先补行为回归：

1. B01 删除期间拖动，以及多图下标错位。
2. B02 笔触/平面/图像/选区途中粘贴，然后撤销与重做。
3. B03 锁边角点、边中点、邻边缩放、重复挤出与多子面删除。
4. B04 QML 快捷键到实际 redo 的集成验证。
5. B05 曲面外推移动必须可撤销；B06 烘焙前后比较重叠图像像素。
6. B07 原地点击和失败角度修改不能创建历史、不能丢失 redo。
7. B08 无最后 mouseMove 的 release，B09 角度锁各入口一致。
8. 地平线附近映射、笔触预算、大角度常数时间归约；后续再补大图内存预算与跨文档状态重置。

## 文件覆盖清单

以下均已阅读；未单独列缺陷的文件表示本次没有额外确定问题，不表示能证明不存在任何 bug。

| 分组 | 文件 |
| --- | --- |
| 入口/构建/资源（3） | src/main.cpp；CMakeLists.txt；res.qrc |
| canvas（4） | vpcanvas.h/.cpp；vpcontroller.h/.cpp |
| core（14） | vpdocument.h/.cpp；scenerenderer.h/.cpp；perspectiveplane.h/.cpp；perspectivequad.h/.cpp；perspectivetransform.h/.cpp；floatingimage.h/.cpp；floatingimageprojection.h/.cpp |
| tools（12） | planecreatetool.h/.cpp；planeedittool.h/.cpp；brushtool.h/.cpp；clonetool.h/.cpp；marqueetool.h/.cpp；floatingimagetransformtool.h/.cpp |
| qml（9） | main.qml；WorkArea.qml；NavigationScrollBar.qml；ZoomControls.qml；ToolBar.qml；ToolButton.qml；ToolOptionsBar.qml；OptionInput.qml；HintBar.qml |
| tests（14） | CMakeLists.txt；testhelpers.h；tst_canvasnavigation.cpp；tst_vpcontroller.cpp；tst_vpdocument.cpp；tst_perspectiveplane.cpp；tst_perspectivequad.cpp；tst_perspectivetransform.cpp；tst_planecreatetool.cpp；tst_planeedittool.cpp；tst_brushtool.cpp；tst_clonetool.cpp；tst_marqueetool.cpp；tst_floatingimageprojection.cpp |

推荐实施顺序：先修 B01~B03 和 B04，再修历史/烘焙/锁定一致性，之后集中交互协调，最后删除未使用接口与优化缓存/构建。此次审核未替用户决定烘焙的产品语义，也未修改已有业务代码。
