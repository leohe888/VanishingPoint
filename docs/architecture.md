# 画布职责边界

- VpCanvas 是 Qt Quick 视图，处理控件坐标转换、缩放、滚动、鼠标光标和重绘。暴露 controller 与导航接口；undo/redo 是兼容转发。只读访问文档用于绘制，不拥有业务工具或可写模型引用。
- VpController 拥有文档与全部工具。pointerPress/Move/Release 接收图像坐标、修饰键和命中所需缩放；不读取控件尺寸，不依赖 QQuickItem。统一协调平面创建/编辑、图像移动/吸附/烘焙、选区取样、画笔/图章、粘贴、删除和历史。独立命令先 cancelInteraction：提交已绘制笔触，丢弃几何与选区预览。
- VpDocument 保存数据与历史，在写入边界验证几何和图像。预览写入由 beginEdit/commitEdit/cancelEdit 包围；完整增删命令在无事务时自动提交，在事务内延迟到 commitEdit。历史比较实际内容，无操作和仅选择变化不删除 redo；绘画用脏区增量，结构用 COW 快照。
- PerspectivePlane 保存拓扑与编辑信息；PerspectiveQuad 是独立几何快照。显示、命中和执行共享锁边规则；内容不依赖后来编辑的平面。
- SceneRenderer 只读文档，renderContent 用于屏幕、仿制源和选区取样，renderFloatingImage 同时用于显示与烘焙。renderGuides 接收明确的 Guides 值和缩放，不保存调用状态、不决定交互权限。
- 工具只计算几何或像素，不拥有历史。没有通用工具基类、命令继承框架或容器。控制器协调具体流程，算法留在工具/几何类型。

烘焙将选中图像及下方图像按原顺序合并到绘画层，保留上方可编辑图像；撤销同时恢复像素与结构。曲面快照、位图裁剪和事务前快照有独立用途，不能因重复存储而移除。
