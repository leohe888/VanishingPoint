# demo 回归测试

使用 AGENTS.md 指定的 Qt 6.11.2 / MinGW 环境，将 Qt bin 与 MinGW bin 加入 PATH 后运行：

~~~powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DVP_ENABLE_GPU_TESTS=ON
cmake --build build -j 6
ctest --test-dir build --output-on-failure -j 1
~~~

20 个常规测试程序默认采用 offscreen / software。启用 VP_ENABLE_GPU_TESTS 后，Windows 上额外运行 D3D11 网格渲染检查 vp_grid_d3d11。剪切板测试必须串行运行。Qt Test 日志位于 build/tests/test-results。

## 测试覆盖

| 测试程序 | 覆盖范围 |
| --- | --- |
| `tst_imagesaving` | 背景/绘画层/浮动选区合成、辅助线排除、PNG 透明度、JPEG/BMP 白底、默认路径、无效请求与写入失败、保存目标重置及活动笔触提交 |
| `tst_perspectivetransform`、`tst_perspectivequad`、`tst_perspectiveplane` | 透视映射、退化输入、命中、控制点、平面连接、状态色与辅助显示 |
| `tst_planecreatetool` | 落点状态、创建提交与失败恢复、取消／工具切换、键盘回退、连续创建与历史、几何阈值、角点顺序与绕序、快照独立性、创建预览颜色与诊断状态、选区附着方向及子平面坐标方向 |
| `tst_planeedittool` | 四角四边编辑、连续更新与非法位置恢复、夹角旋转及跨 ±180°、派生预览与父子连接、共享边锁定、历史与取消／工具切换、缩放命中及 Ctrl 拖动阈值、状态色与编辑光标 |
| `tst_marqueetool` | 透视下四向创建与最小尺寸、Shift 单轴和网格吸附、动作生命周期、同组平面与几何快照、轮廓缓存、透明与越界像素提取、8192 边长／64 MiB 与羽化边界、非整数缩放预览、羽化／不透明度／修复、目标／源模式、创建／移动／填充中断恢复、粘贴取消填充、历史边界与源填充缓存失效、跨工具保留及绘制裁剪 |
| `tst_brushtool` | 参数与硬度梯度、笔触生命周期、单击及松手末段、四向连续补点、透视映射锁定与几何快照、画布四边四角与越界裁剪、非法坐标／投影奇点／超长移动恢复、写入前备份及重绘区域、两笔历史与空操作保留重做、工具切换／取消／撤销／粘贴中断、局部纹理和缩放预览、笔中参数变化及修复重放 |
| `tst_clonetool` | 图章笔触、透视、采样、预览、历史，以及悬停目标平面和笔触期间的目标锁定 |
| `tst_eyedroppertool` | 合成内容取色、采样缓存、无效位置处理、颜色通知及吸管光标热点 |
| `tst_healingblend` | 画笔和图章的修复模式、完成时机、覆盖范围、透视预览、撤销重做与透明背景回退 |
| `tst_floatingselectiontransformtool` | 缩放、旋转、水平／垂直翻转、几何与历史、变换光标方向和拖动期间的光标保持 |
| `tst_vpdocument` | 文档事务、撤销重做、后代平面删除、索引调整及共享边解锁 |
| `tst_vpcontroller` | 工具可用性、属性通知、图章文档采样，以及剪切板粘贴、透明度和复制来源优先级 |
| `tst_floatingselectionprojection` | 浮动选区投影、命中、控制点、缓存、跨曲面裁剪与像素恢复，以及旋转／缩放后的蚂蚁线显示 |
| `tst_canvasnavigation` | 控制器生命周期、缩放和平移、临时抓手、导航光标、目标平面高亮渲染、画布边界裁剪及辅助轮廓保留；AC12拖选精度120组数据（50%／100%／200%、矩形／透视梯形、中心／四边、居中／滚动平移、正反向拖选），检查拖动中与松手后四角偏差≤2 Qt逻辑显示像素 |
| `tst_auditinteractions`、`tst_auditboundaries` | 跨工具操作中断、粘贴、异常输入与历史数量上限 |
| `tst_qmlintegration` | QML 绑定、真实滚动条和提示栏组件、临时消息替换原提示、3 秒后恢复与重复计时、错误颜色、交互进度保留、工具切换清理、画布重做调用和视图重建后的控制器所有权 |

功能检查统一由以上测试类中的测试方法执行；通用光标尺寸与热点断言放在 `cursorhelpers.h`。光标测试直接使用仓库中的 SVG/PNG 资源，无需额外传入资源路径。QML 测试使用最小宿主验证画布、导航和真实提示栏组件，主软件的完整对话框仍需在主程序中验证。demo 的 tst_qmlintegration 额外覆盖独立窗口、工具栏、选项栏、临时抓手高亮、全窗口复制/取消选区快捷键，以及数值滑条/标签拖动/滚轮/键盘提交和绑定保持；tst_qmlboundaries 对应主模块的最小宿主集成测试。

选框尺寸限制和填充预览被粘贴中断的检查统一归入 `tst_marqueetool`；审计测试保留平面／图像的粘贴中断和通用边界检查。轮廓缓存复用与失效有独立功能断言，日常回归无需运行性能程序。

画笔整笔历史、粘贴结束笔触和超长移动段的检查统一归入 `tst_brushtool`。画笔取消交互／切换工具会提交已绘制的笔触，可随后整笔撤销；修复的共性断言保留在 `tst_healingblend`，选框跨工具裁剪保留在 `tst_marqueetool`。

## demo 的渲染适配

- tst_canvasnavigation 的独立绘制项挂接 QQuickWindow，以适配 Qt 6 的可见性规则。
- tst_marqueetool 用抗锯齿裁剪蒙版检查选区外像素，覆盖 Qt 6 的多边形边缘栅格化。
- tst_floatingselectionprojection 适配 Qt 6 渲染控制接口，并增加 D3D11 网格四象限检查。默认软件用例会跳过这项 GPU 检查，CTest 的独立 gpu 用例负责执行。
- 主项目最新画笔、审计和控制器测试源码保持一致；画笔硬度测试同时防止 Qt 6 在精确渐变中心产生透明点。

## 可选性能检查

配置 -DVP_BUILD_BENCHMARKS=ON 会构建 benchmarks 中的 5 个交互性能检查程序，不加入常规 CTest。运行需可用桌面，结果不计入日常回归测试通过数。
