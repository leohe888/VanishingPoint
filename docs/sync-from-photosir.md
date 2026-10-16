# 主项目消失点模块回同步

同步日期：2026-10-16。来源：main_v3_feature_vanishing_point 的 feature_vanishing_point 分支，提交 b123976b3。

## 同步范围

- 同步 canvas、core、tools 的当前实现，包括透视平面坐标方向统一、悬停高亮、选框保留和绘制裁剪、源模式填充、浮动选区翻转及图像边界裁剪、修复处理、吸管取色与统一光标。
- 同步分层画布、辅助线场景图、局部刷新、浮动内容投影缓存等性能改进。
- 提示栏显示工具说明及交互进度；操作反馈临时替换提示，3 秒后恢复，错误使用红色。
- demo 界面增加吸管、变换翻转、画笔/图章修复、选框羽化/不透明度/修复/移动模式选项；缩小按钮移动到缩放比例右侧。
- 同步按工具组织的回归测试及可选性能检查，保留 demo 整个窗口的 QML 集成测试。

## demo 适配

- 继续使用 Qt 6.11.2 / MinGW 和独立图片打开对话框。主项目传入图像的 setImage 接口保留，demo 的 openImage 在读取成功后调用它，保留 128 MiB 图片加载限制。
- 源码布局与主模块对齐：VpDocument 移到 canvas；FloatingImage 系列改名为 FloatingSelection 系列。
- Qt 6 使用 geometryChange 和鼠标 position；离屏 OpenGL 测试使用 Qt 6 的图形设备与渲染目标接口。
- 界面继续使用 VpTheme 和 demo 原有组件；主软件的图层写回、主窗口入口、主题服务、翻译服务和颜色选择服务由 demo 自身的文件入口、Qt Quick 控件及 ColorDialog 对应。
- 光标 SVG 放在 assets/cursors，吸管图标放在 assets/icons，并编入 res.qrc。

## 构建与验证

使用 AGENTS.md 中的 Qt、CMake、Ninja、MinGW 路径，将 Qt bin 与 MinGW bin 加入 PATH 后运行：

~~~powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 6
ctest --test-dir build --output-on-failure -j 1
~~~

Qt Test 文本报告保存在 build/tests/test-results。性能检查可通过 -DVP_BUILD_BENCHMARKS=ON 构建，按需交互运行，不加入默认 CTest。

本次验证：Debug 主程序与 5 个性能检查程序构建通过，19/19 CTest 通过，OpenGL 选区轮廓检查通过。Qt 6 的 offscreen 平台不提供 OpenGL 上下文，GPU 检查需使用 QT_QPA_PLATFORM=windows、QSG_RHI_BACKEND=opengl、VP_TEST_OPENGL=1，并清空 QT_QUICK_BACKEND。
