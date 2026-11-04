# Compile FrenchifyTabOverlayNative (Lunar MCP) and regenerate lunar_headers/FrenchifyTabOverlayNativeLunar.h
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stubs = Join-Path $root "java_src\stubs_lunar"
$src = Join-Path (Split-Path $PSScriptRoot -Parent) "java_src\FrenchifyTabOverlayNativeLunar.java"
$out = Join-Path $root "build\tab_overlay_lunar_compile"
$lunarHeaders = Join-Path $root "java_src\lunar_headers\FrenchifyTabOverlayNativeLunar.h"
$releaseHeaders = Join-Path (Split-Path $PSScriptRoot -Parent) "java_src\lunar_headers\FrenchifyTabOverlayNativeLunar.h"

New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item $src (Join-Path $out "FrenchifyTabOverlayNative.java") -Force

javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $out (Join-Path $out "FrenchifyTabOverlayNative.java")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$classPath = Join-Path $out "FrenchifyTabOverlayNative.class"
$bytes = [System.IO.File]::ReadAllBytes($classPath)
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("// Auto-generated Lunar bytecode for FrenchifyTabOverlayNative (extends GuiPlayerTabOverlay MCP)")
[void]$sb.AppendLine("static const unsigned char FrenchifyTabOverlayNativeLunar_class[] = {")
for ($i = 0; $i -lt $bytes.Length; $i += 16) {
    $chunk = @()
    $end = [Math]::Min($i + 15, $bytes.Length - 1)
    for ($j = $i; $j -le $end; $j++) { $chunk += ("0x{0:X2}" -f $bytes[$j]) }
    [void]$sb.AppendLine("    " + ($chunk -join ", ") + ",")
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine(("static const unsigned int FrenchifyTabOverlayNativeLunar_class_len = {0};" -f $bytes.Length))
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($lunarHeaders, $sb.ToString(), $utf8NoBom)
Copy-Item $lunarHeaders $releaseHeaders -Force
Write-Host "OK: FrenchifyTabOverlayNative -> $($bytes.Length) bytes -> $lunarHeaders"
Write-Host "Copied -> $releaseHeaders"
