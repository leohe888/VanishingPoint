param(
    [string]$QtRoot = 'D:\Software\Qt\6.11.2\mingw_64',
    [string]$ToolsRoot = 'D:\Software\Qt\Tools'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build/package-release'
$packageName = 'VanishingPoint-Windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$packageDir = Join-Path $repoRoot "dist/$packageName"
$archivePath = "$packageDir.zip"
$logPath = Join-Path $buildDir "$packageName.log"
$previousPath = $env:PATH

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Path $packageDir | Out-Null
Start-Transcript -Path $logPath | Out-Null
try {
    $env:PATH = "$QtRoot\bin;$ToolsRoot\mingw1310_64\bin;$ToolsRoot\Ninja;$ToolsRoot\CMake_64\bin;$previousPath"
    $cmake = Join-Path $ToolsRoot 'CMake_64/bin/cmake.exe'
    & $cmake -S $repoRoot -B $buildDir -G Ninja `
        '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH=$QtRoot" `
        "-DCMAKE_CXX_COMPILER=$ToolsRoot/mingw1310_64/bin/g++.exe" `
        '-DBUILD_TESTING=OFF'
    if ($LASTEXITCODE -ne 0) { throw 'Release configuration failed.' }

    & $cmake --build $buildDir --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

    $executable = Join-Path $packageDir 'VanishingPoint.exe'
    Copy-Item -LiteralPath (Join-Path $buildDir 'VanishingPoint.exe') -Destination $executable
    & "$QtRoot/bin/windeployqt.exe" --release --compiler-runtime `
        --qmldir (Join-Path $repoRoot 'src/qml') $executable
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }

    foreach ($dependency in @('Qt6Core.dll', 'platforms/qwindows.dll',
                              'libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $packageDir $dependency))) {
            throw "Missing deployed dependency: $dependency"
        }
    }
    Compress-Archive -LiteralPath $packageDir -DestinationPath $archivePath
    Write-Output "Package: $archivePath"
    Write-Output "Directory: $packageDir"
    Write-Output "Build/deployment log: $logPath"
    Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
}
finally {
    $env:PATH = $previousPath
    Stop-Transcript | Out-Null
}
