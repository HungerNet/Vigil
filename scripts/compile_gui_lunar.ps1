# Compile VigilGui4Lunar.java and regenerate lunar_headers/VigilGuiLunar.h
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$stubs = Join-Path $root "java_src\stubs_lunar"
$maker = Join-Path $PSScriptRoot "make_lunar_gui.py"
$srcGen = Join-Path $root "java_src\VigilGui4Lunar.java"
$out = Join-Path $root "build\gui_lunar_compile"
$header = Join-Path $root "java_src\lunar_headers\VigilGuiLunar.h"

python $maker
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

New-Item -ItemType Directory -Force -Path $out | Out-Null
$src = Join-Path $out "VigilGui4.java"
Copy-Item $srcGen $src -Force
javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $out $src
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$classPath = Join-Path $out "VigilGui4.class"
$bytes = [System.IO.File]::ReadAllBytes($classPath)
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("// Auto-generated Lunar bytecode for VigilGui4 (MCP)")
[void]$sb.AppendLine("static const unsigned char VigilGuiLunar_class[] = {")
for ($i = 0; $i -lt $bytes.Length; $i += 16) {
    $chunk = @()
    $end = [Math]::Min($i + 15, $bytes.Length - 1)
    for ($j = $i; $j -le $end; $j++) { $chunk += ("0x{0:X2}" -f $bytes[$j]) }
    [void]$sb.AppendLine("    " + ($chunk -join ", ") + ",")
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine(("static const unsigned int VigilGuiLunar_class_len = {0};" -f $bytes.Length))
[System.IO.File]::WriteAllText($header, $sb.ToString())
Write-Host "OK: $($bytes.Length) bytes -> $header"
