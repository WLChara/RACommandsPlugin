[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$GameSdkVersion = 'mo-7cd005d2'
)

$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$sourceRoot = Join-Path $projectRoot 'src'
$sdkRoot = Join-Path $sourceRoot 'SDK\YRpp'
$sdkPath = Join-Path $sdkRoot $GameSdkVersion
if (-not (Test-Path -LiteralPath (Join-Path $sdkPath 'YRPPCore.h') -PathType Leaf)) {
    throw "Missing YRpp SDK version: $sdkPath"
}

$sourceFiles = @(Get-ChildItem -LiteralPath $sourceRoot -Filter '*.cpp' -File -Recurse |
    Where-Object { $_.FullName -notlike "$sourceRoot\SDK\*" -and
                   $_.FullName -notlike "$sourceRoot\Tests\*" })
if ($sourceFiles.Count -eq 0) {
    throw 'Build gate: src 中尚无 .cpp 源文件；待插件实现到位后再运行构建。'
}

$libraryPath = Join-Path $projectRoot 'third_party\libhat\Win32\Release\v143\static\libhat.lib'
if (-not (Test-Path -LiteralPath $libraryPath -PathType Leaf)) {
    throw "Missing Win32 Release libhat library: $libraryPath"
}

$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
    throw "Visual Studio locator was not found: $vswherePath"
}

$installationPaths = @(& $vswherePath -version '[17.0,18.0)' -products '*' -requires Microsoft.Component.MSBuild -property installationPath)
$vswhereExitCode = $LASTEXITCODE
if ($vswhereExitCode -ne 0 -or $installationPaths.Count -eq 0) {
    throw 'Visual Studio 2022 with MSBuild was not found.'
}

$installationPath = $installationPaths[0]
$msbuildPath = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuildPath -PathType Leaf)) {
    throw "MSBuild was not found: $msbuildPath"
}

$solutionPath = Join-Path $projectRoot 'RACommandsPlugin.sln'
& $msbuildPath $solutionPath '/m' '/p:Configuration=Release' '/p:Platform=Win32' "/p:GameSdkVersion=$GameSdkVersion" '/verbosity:minimal'
if ($LASTEXITCODE -ne 0) {
    throw "RACommandsPlugin Win32 Release build failed with exit code $LASTEXITCODE."
}

$testsPath = Join-Path $projectRoot 'build\Win32\Release\RACommandsPlugin.Tests.exe'
if (-not (Test-Path -LiteralPath $testsPath -PathType Leaf)) {
    throw "Pure test executable was not produced: $testsPath"
}
& $testsPath
if ($LASTEXITCODE -ne 0) {
    throw "RACommandsPlugin pure tests failed with exit code $LASTEXITCODE."
}

Write-Host (Join-Path $projectRoot 'build\Win32\Release\RACommandsPlugin.dll')
