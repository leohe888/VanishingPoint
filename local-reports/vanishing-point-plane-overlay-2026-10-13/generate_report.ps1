$ErrorActionPreference = 'Stop'
$folder = $PSScriptRoot
$source = 'D:\Code\main_v3_feature_vanishing_point\build\plane_overlay_perf'
$rows = foreach ($side in @(4096,8192)) {
    foreach ($round in 1..5) {
        foreach ($version in @('baseline','optimized')) {
            $path = Join-Path $source "$version-$side-$round.json"
            $data = Get-Content -Raw $path | ConvertFrom-Json
            if (-not $data.passed -or $data.results.Count -ne 5) { throw "Invalid result $path" }
            Copy-Item -LiteralPath $path -Destination $folder
            foreach ($result in $data.results) {
                [pscustomobject]@{
                    Version=$version; ImageSide=$side; Round=$round; Operation=$result.operation
                    LatencyP95Ms=$result.latencyP95Ms; LatencyP99Ms=$result.latencyP99Ms
                    IntervalP95Ms=$result.intervalP95Ms; LongIntervalPercent=$result.longIntervalPercent
                    ContentPaintCount=$result.contentPaintCount; ContentPaintP95Ms=$result.contentPaintP95Ms
                    HandlerP95Ms=$result.handlerP95Ms; InputEvents=$result.inputEvents
                    PreviewFrames=$result.previewFrames; PlaneBuilds=$result.planeBuilds
                }
            }
        }
    }
}
$rows | Export-Csv -NoTypeInformation -Encoding utf8 (Join-Path $folder 'summary.csv')
function Median($items, $metric) {
    $values = @($items | ForEach-Object { [double]$_.$metric } | Sort-Object)
    return $values[[int][Math]::Floor($values.Count / 2)]
}
function F($value) { return ([double]$value).ToString('F3', [Globalization.CultureInfo]::InvariantCulture) }
$names = @{move='平移';resize_edge='边延长/缩短';extrude='拉出垂直面';rotate_drag='Alt 鼠标调角';angle_parameter='角度参数设置'}
$operations = @('move','resize_edge','extrude','rotate_drag','angle_parameter')
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('下表采用各轮 P95 的中位数，降低率以两版中位数计算；每项均有 5 轮有效结果。')
$lines.Add('')
$lines.Add('| 图像 | 操作 | 旧延迟 P95（ms） | 新延迟 P95（ms） | 降低率 | 旧更新间隔 P95（ms） | 新更新间隔 P95（ms） |')
$lines.Add('|---|---|---:|---:|---:|---:|---:|')
foreach ($side in @(4096,8192)) {
    foreach ($operation in $operations) {
        $old = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Operation -eq $operation -and $_.Version -eq 'baseline' })
        $new = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Operation -eq $operation -and $_.Version -eq 'optimized' })
        $before = Median $old 'LatencyP95Ms'; $after = Median $new 'LatencyP95Ms'
        $reduction = 100 * ($before - $after) / $before
        $lines.Add("| $side | $($names[$operation]) | $(F $before) | $(F $after) | $(F $reduction)% | $(F (Median $old 'IntervalP95Ms')) | $(F (Median $new 'IntervalP95Ms')) |")
    }
}
$lines.Add('')
$lines.Add('操作处理和缓存成本如下，同样采用各轮中位数。内容层绘制次数是每种操作的 2 秒测量窗口内计数。')
$lines.Add('')
$lines.Add('| 图像 | 操作 | 旧处理 P95（ms） | 新处理 P95（ms） | 旧内容绘制次数 | 新内容绘制次数 | 新输入次数 | 新辅助几何重建次数 |')
$lines.Add('|---|---|---:|---:|---:|---:|---:|---:|')
foreach ($side in @(4096,8192)) {
    foreach ($operation in $operations) {
        $old = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Operation -eq $operation -and $_.Version -eq 'baseline' })
        $new = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Operation -eq $operation -and $_.Version -eq 'optimized' })
        $lines.Add("| $side | $($names[$operation]) | $(F (Median $old 'HandlerP95Ms')) | $(F (Median $new 'HandlerP95Ms')) | $(Median $old 'ContentPaintCount') | $(Median $new 'ContentPaintCount') | $(Median $new 'InputEvents') | $(Median $new 'PlaneBuilds') |")
    }
}
$lines.Add('')
$newAll = @($rows | Where-Object Version -eq 'optimized')
$paintMax = ($newAll | Measure-Object ContentPaintCount -Maximum).Maximum
$lines.Add("新版全部 50 个操作测量窗口的内容层绘制次数最大值为 $paintMax。缓存检查要求辅助几何重建次数不超过输入更新次数加 2，全部通过；同时操作前后的实际几何与显示像素检查通过。这支持每次主要重建活动平面/候选面、保留三个静态平面缓存的结论。")
$lines.Add('')
$lines.Add('新版处理函数承担了顶点和软件绘制指令的准备工作，耗时高于只更新模型的旧版处理函数。这项新增成本已计入，不隐藏在“绘制次数为 0”的表述中；整体帧提交延迟和更新间隔仍明显下降。')
$lines.Add('')
$lines.Add('逐轮波动范围：')
$lines.Add('')
$lines.Add('| 图像 | 操作 | 旧延迟 P95 范围（ms） | 新延迟 P95 范围（ms） | 旧长停顿比例中位数 | 新长停顿比例中位数 |')
$lines.Add('|---|---|---:|---:|---:|---:|')
foreach ($side in @(4096,8192)) {
    foreach ($operation in $operations) {
        $old = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Operation -eq $operation -and $_.Version -eq 'baseline' })
        $new = @($rows | Where-Object { $_.ImageSide -eq $side -and $_.Operation -eq $operation -and $_.Version -eq 'optimized' })
        $oldRange = $old | Measure-Object LatencyP95Ms -Minimum -Maximum
        $newRange = $new | Measure-Object LatencyP95Ms -Minimum -Maximum
        $lines.Add("| $side | $($names[$operation]) | $(F $oldRange.Minimum)–$(F $oldRange.Maximum) | $(F $newRange.Minimum)–$(F $newRange.Maximum) | $(F (Median $old 'LongIntervalPercent'))% | $(F (Median $new 'LongIntervalPercent'))% |")
    }
}
$lines.Add('')
$lines.Add('截图：[平移](optimized-move.png)、[边伸缩](optimized-resize.png)、[拉出预览](optimized-extrude.png)、[调角](optimized-rotate.png)、[稠密网格](dense-grid.png)、[创建预览](creation.png)。')
$path = Join-Path $folder 'report.md'
$report = [IO.File]::ReadAllText($path).Replace('{{RESULTS}}', ($lines -join "`n"))
[IO.File]::WriteAllText($path, $report, [Text.UTF8Encoding]::new($false))
$rows | Select-Object Version,ImageSide,Operation,LatencyP95Ms,ContentPaintCount | Out-Null
