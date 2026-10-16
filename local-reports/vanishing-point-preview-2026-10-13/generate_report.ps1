$ErrorActionPreference = 'Stop'
$folder = $PSScriptRoot
$rows = Import-Csv (Join-Path $folder 'summary.csv')
function Median($items, $metric) {
    $values = @($items | ForEach-Object { [double]$_.$metric } | Sort-Object)
    return $values[[int][Math]::Floor($values.Count / 2)]
}
function F($value) { return ([double]$value).ToString('F3', [Globalization.CultureInfo]::InvariantCulture) }
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('| 图像 | 指标 | 旧版 | 新版 | 降低率 |')
$lines.Add('|---|---|---:|---:|---:|')
foreach ($side in @(4096,8192)) {
    $old = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Version -eq 'baseline' })
    $new = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Version -eq 'optimized' })
    foreach ($metric in @('LatencyP95Ms','IntervalP95Ms','ContentPaintCount','ContentPaintTotalMs','LongIntervalPercent')) {
        $label = switch ($metric) {
            LatencyP95Ms { '帧提交延迟 P95（ms）' }
            IntervalP95Ms { '更新间隔 P95（ms）' }
            ContentPaintCount { '内容层绘制次数 / 轮' }
            ContentPaintTotalMs { '累计内容层绘制壁钟时间（ms）/ 轮' }
            LongIntervalPercent { '长停顿比例（%）' }
        }
        $before = Median $old $metric
        $after = Median $new $metric
        $reduction = if ($before -gt 0) { "$(F (100 * ($before - $after) / $before))%" } else { '—' }
        $lines.Add("| $side | $label | $(F $before) | $(F $after) | $reduction |")
    }
}
$lines.Add('')
$lines.Add('两个图像组的新版在全部 5 轮中均未发生内容层绘制；表中 0 ms 表示没有调用内容层绘制，不是一次绘制只需 0 ms。')
$lines.Add('')
$lines.Add('逐轮结果如下，便于观察波动：')
$lines.Add('')
$lines.Add('| 图像 | 轮次 | 旧延迟 P95（ms） | 新延迟 P95（ms） | 旧更新间隔 P95（ms） | 新更新间隔 P95（ms） | 旧绘制次数 | 新绘制次数 |')
$lines.Add('|---|---:|---:|---:|---:|---:|---:|---:|')
foreach ($side in @(4096,8192)) {
    foreach ($round in 1..5) {
        $old = $rows | Where-Object { $_.ImageSide -eq $side -and $_.Version -eq 'baseline' -and $_.Round -eq $round }
        $new = $rows | Where-Object { $_.ImageSide -eq $side -and $_.Version -eq 'optimized' -and $_.Round -eq $round }
        $lines.Add("| $side | $round | $(F $old.LatencyP95Ms) | $(F $new.LatencyP95Ms) | $(F $old.IntervalP95Ms) | $(F $new.IntervalP95Ms) | $($old.ContentPaintCount) | $($new.ContentPaintCount) |")
    }
    $oldGroup = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Version -eq 'baseline' })
    $newGroup = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Version -eq 'optimized' })
    $oldRange = $oldGroup | ForEach-Object { [double]$_.LatencyP95Ms } | Measure-Object -Minimum -Maximum
    $newRange = $newGroup | ForEach-Object { [double]$_.LatencyP95Ms } | Measure-Object -Minimum -Maximum
    $lines.Add('')
    $lines.Add("$side 的逐轮延迟 P95 范围：旧版 $(F $oldRange.Minimum)–$(F $oldRange.Maximum) ms，新版 $(F $newRange.Minimum)–$(F $newRange.Maximum) ms。")
    $lines.Add('')
    if ($side -eq 4096) {
        $lines.Add('| 图像 | 轮次 | 旧延迟 P95（ms） | 新延迟 P95（ms） | 旧更新间隔 P95（ms） | 新更新间隔 P95（ms） | 旧绘制次数 | 新绘制次数 |')
        $lines.Add('|---|---:|---:|---:|---:|---:|---:|---:|')
    }
}
$lines.Add('')
$lines.Add('旧版内容层单次 paint 的 P95 中位数：4096 组约 '+(F (Median @($rows | Where-Object { $_.ImageSide -eq 4096 -and $_.Version -eq 'baseline' }) 'ContentPaintP95Ms'))+' ms，8192 组约 '+(F (Median @($rows | Where-Object { $_.ImageSide -eq 8192 -and $_.Version -eq 'baseline' }) 'ContentPaintP95Ms'))+' ms。与两版毫秒以下的 hover 处理时间一起，支持“重复绘制内容层是主要额外成本”的判断。')
$lines.Add('')
$lines.Add('对比截图：[旧版](baseline.png)、[新版硬件覆盖层](optimized.png)、[软件兼容覆盖层](software.png)、[DPR=2 原点检查](high-dpi-origin.png)。')
$validation = @'
| 检查 | 结果 |
|---|---|
| 主工程 Debug x64 编译与链接（独立输出名） | 通过 |
| Qt 5.15 Release 新旧独立检查程序 | 通过 |
| OpenGL 后端：实际连线与角点像素、图像原点、离开后隐藏鼠标端点且保留固定边 | 通过 |
| 软件后端：同上 | 通过 |
| DPR=2：同上 | 通过 |
| 三个后端/缩放检查：缩放、滚动、改变窗口尺寸后固定角点的像素位置与变换一致 | 通过 |
| 取消、切换工具、四次点击完成平面、撤销重做、文档替换、控制器销毁 | 通过 |
| 20 次正式性能运行：每个角点阶段至少 50 个有效帧，三角点阶段均完成 | 通过 |
| git diff --check | 通过 |

功能结果分别保存在 `functional.json`、`software-functional.json`、`high-dpi-functional.json`，失败列表均为空。性能与截图检查均使用实际 Qt 渲染路径。
'@
$reportPath = Join-Path $folder 'report.md'
$report = [IO.File]::ReadAllText($reportPath)
$report = $report.Replace('{{RESULTS}}', ($lines -join "`n")).Replace('{{VALIDATION}}', $validation)
[IO.File]::WriteAllText($reportPath, $report, [Text.UTF8Encoding]::new($false))
