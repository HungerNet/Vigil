# Compile FrenchifyGui4Lunar.java and regenerate lunar_headers/FrenchifyGuiLunar.h
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stubs = Join-Path $root "java_src\stubs_lunar"
$maker = Join-Path $PSScriptRoot "make_lunar_gui.py"
$srcGen = Join-Path $root "java_src\FrenchifyGui4Lunar.java"
$out = Join-Path $root "build\gui_lunar_compile"
$header = Join-Path $root "java_src\lunar_headers\FrenchifyGuiLunar.h"

python $maker
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

New-Item -ItemType Directory -Force -Path $out | Out-Null
$src = Join-Path $out "FrenchifyGui4.java"
Copy-Item $srcGen $src -Force
javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $out $src
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$classPath = Join-Path $out "FrenchifyGui4.class"
$bytes = [System.IO.File]::ReadAllBytes($classPath)
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("// Auto-generated Lunar bytecode for FrenchifyGui4 (MCP)")
[void]$sb.AppendLine("static const unsigned char FrenchifyGuiLunar_class[] = {")
for ($i = 0; $i -lt $bytes.Length; $i += 16) {
    $chunk = @()
    $end = [Math]::Min($i + 15, $bytes.Length - 1)
    for ($j = $i; $j -le $end; $j++) { $chunk += ("0x{0:X2}" -f $bytes[$j]) }
    [void]$sb.AppendLine("    " + ($chunk -join ", ") + ",")
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine(("static const unsigned int FrenchifyGuiLunar_class_len = {0};" -f $bytes.Length))
[System.IO.File]::WriteAllText($header, $sb.ToString())
$releaseHeader = Join-Path (Split-Path $PSScriptRoot -Parent) "java_src\lunar_headers\FrenchifyGuiLunar.h"
Copy-Item $header $releaseHeader -Force
Write-Host "OK: $($bytes.Length) bytes -> $header"
Write-Host "Copied -> $releaseHeader"
