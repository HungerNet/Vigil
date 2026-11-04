# Compile VigilGui4Lunar.java -> VigilGuiLunar.h without Python make_lunar_gui.py
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stubs = Join-Path $root "java_src\stubs_lunar"
$srcGen = Join-Path $root "java_src\VigilGui4Lunar.java"
$out = Join-Path $root "build\gui_lunar_compile"
$header = Join-Path $root "java_src\lunar_headers\VigilGuiLunar.h"
$releaseHeader = Join-Path (Split-Path $PSScriptRoot -Parent) "java_src\lunar_headers\VigilGuiLunar.h"

New-Item -ItemType Directory -Force -Path $out | Out-Null
$src = Join-Path $out "VigilGui4.java"
Copy-Item $srcGen $src -Force
& javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $out $src
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$classPath = Join-Path $out "VigilGui4.class"
$bytes = [System.IO.File]::ReadAllBytes($classPath)
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("// Auto-generated Lunar bytecode for VigilGui4 (MCP)")
[void]$sb.AppendLine("static const unsigned char VigilGuiLunar_class[] = {")
for ($i = 0; $i -lt $bytes.Length; $i += 16) {
    $chunk = New-Object System.Collections.Generic.List[string]
    $end = [Math]::Min($i + 15, $bytes.Length - 1)
    for ($j = $i; $j -le $end; $j++) {
        [void]$chunk.Add(('0x{0:X2}' -f $bytes[$j]))
    }
    [void]$sb.AppendLine(('    {0},' -f ($chunk -join ', ')))
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine(('static const unsigned int VigilGuiLunar_class_len = {0};' -f $bytes.Length))
[System.IO.File]::WriteAllText($header, $sb.ToString())
New-Item -ItemType Directory -Force -Path (Split-Path $releaseHeader -Parent) | Out-Null
Copy-Item $header $releaseHeader -Force
Write-Host ("OK: {0} bytes -> {1}" -f $bytes.Length, $header)
Write-Host ("Copied -> {0}" -f $releaseHeader)
