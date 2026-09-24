$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -prerelease -products * -property installationPath
$vc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Lib" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$kit = Split-Path (Split-Path $sdk.FullName -Parent) -Parent
$env:INCLUDE = "$($vc.FullName)\include;$kit\Include\$($sdk.Name)\ucrt;$kit\Include\$($sdk.Name)\shared;$kit\Include\$($sdk.Name)\um"
$env:LIB = "$($vc.FullName)\lib\x86;$($sdk.FullName)\ucrt\x86;$($sdk.FullName)\um\x86"
$objects = Get-ChildItem "$repo\Release\*.obj" | Where-Object { $_.Name -ne 'main.obj' -and $_.Name -ne 'PinnedImage.obj' -and $_.Name -notlike '*_test.obj' } | ForEach-Object FullName
$compiler = "$($vc.FullName)\bin\Hostx64\x86\cl.exe"
& $compiler /nologo /W3 /WX /O2 /MT /Gy /DWIN32 /D_WINDOWS /DOP_XP_STYLE /DLC_JP /DUNICODE /D_UNICODE /I. tests\menu_regression.c /FoRelease\menu_regression_test.obj /FeRelease\menu_regression_test.exe $objects Release\CLCL.res /link /SUBSYSTEM:CONSOLE /OPT:REF user32.lib gdi32.lib comctl32.lib imm32.lib winsqlite3.lib shell32.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib comdlg32.lib
if ($LASTEXITCODE -ne 0) { throw 'Regression compilation failed' }
& .\Release\menu_regression_test.exe
if ($LASTEXITCODE -ne 0) { throw "Menu regression failed: exit $LASTEXITCODE" }
