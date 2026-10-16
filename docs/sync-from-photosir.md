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

### D3D11 网格裁剪回归

2026-10-16 补充修复：Qt 6 的 D3D11 RHI 不支持 DrawTriangleFan。平面网格裁剪改用两个显式三角形，避免只显示对角线一侧。此前 OpenGL 轮廓检查不覆盖该路径。

在 Windows 上配置 -DVP_ENABLE_GPU_TESTS=ON 后，CTest 会额外执行 vp_grid_d3d11，在四个象限检查网格可见性。测试截图保存在 build/tests/plane-grid-gpu.png。

### 功能及交互补齐

同步至主项目 62101d65a 的画笔收尾及完整画笔回归；demo 增加临时抓手工具栏高亮，数值控件具备弹出滑条、拖动标签、滚轮调节及实时预览，并在隐藏/禁用时关闭弹窗。整窗集成测试验证这些交互及控制器绑定。

Qt 6 硬度测试额外暴露了精确像素中心的径向渐变透明点，demo 对渐变中心使用 0.001 像素偏移兼容，保持笔刷轮廓及中心覆盖。这与 API、网格三角形裁剪和网格吸附舍入一样属于运行环境适配。

### demo 图片保存

demo 现有独立保存按钮，图标来自主项目深色皮肤的 top_icon/image_save.svg。Ctrl+S 首次选择文件后直接保存到该目标，Ctrl+Shift+S 另存为。默认导出 PNG，也可选择 JPEG 或 BMP。保存输出为原尺寸合成图片，不包括辅助线，也不包含可继续编辑的平面定义。

### 2025f0e79 图章增量同步

同步图章松手末段与失败取样映射保留修复，以及最新的图章、控制器和审计交互测试；测试覆盖说明同步更新。demo 自有保存功能、界面调整与 Qt 6 适配继续保留。

### 408455db4 吸管测试增量同步

完整同步主项目扩充后的吸管回归与覆盖说明；新增透明合成、透视内容、缓存与历史状态，以及缩放/滚动下真实鼠标取色检查。本次主项目未修改功能代码。

### eb4485502 缩放与文档事务增量同步

补齐 b6d70cacd 的缩放导航恢复与独立缩放测试，以及 eb4485502 的事务期间延后历史提交和扩充的文档/工具回归。此前抓手测试和状态清理已由 cd743fc 同步。导航测试保留 Qt 6 的绘制项挂接窗口适配；demo 的保存功能、界面调整及 local-reports 归档保留。

### a2ecd3013 浮动选区变换增量同步

同步主项目的变换输入校验与失败启动恢复：拒绝空图像、无效尺寸和非有限坐标，启动失败时保留已有变换状态。完整同步扩充后的变换回归及覆盖说明，涵盖八控制点、Shift/Alt、旋转吸附、透视附着、翻转羽化、交互中断及历史。demo 原有功能与 Qt 6 适配保留。
