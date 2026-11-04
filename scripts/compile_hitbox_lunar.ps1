# Compile Lunar hitbox hook classes and regenerate lunar_headers/*.h
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stubs = Join-Path $root "java_src\stubs_lunar"
$out = Join-Path $root "build\hitbox_lunar_compile"
$lunarHeaders = Join-Path $root "java_src\lunar_headers"
$releaseHeaders = Join-Path (Split-Path $PSScriptRoot -Parent) "java_src\lunar_headers"

New-Item -ItemType Directory -Force -Path $out | Out-Null

$rgSrc = Join-Path $root "java_src\FrenchifyRenderGlobalLunar.java"
$hiSrc = Join-Path $root "java_src\FrenchifyHitboxInstallerLunar.java"
$pumpSrc = Join-Path $root "java_src\FrenchifyClientPumpLunar.java"
Copy-Item $rgSrc (Join-Path $out "FrenchifyRenderGlobal.java") -Force
Copy-Item $hiSrc (Join-Path $out "FrenchifyHitboxInstaller.java") -Force
Copy-Item $pumpSrc (Join-Path $out "FrenchifyClientPump.java") -Force

javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $out (Join-Path $out "FrenchifyRenderGlobal.java") (Join-Path $out "FrenchifyHitboxInstaller.java") (Join-Path $out "FrenchifyClientPump.java")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

function Write-ClassHeader($className, $headerPath, $arrayName, $title) {
    $classPath = Join-Path $out "$className.class"
    $bytes = [System.IO.File]::ReadAllBytes($classPath)
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("#pragma once")
    [void]$sb.AppendLine("// $title")
    [void]$sb.AppendLine("static const unsigned char ${arrayName}[] = {")
    for ($i = 0; $i -lt $bytes.Length; $i += 16) {
        $chunk = @()
        $end = [Math]::Min($i + 15, $bytes.Length - 1)
        for ($j = $i; $j -le $end; $j++) { $chunk += ("0x{0:X2}" -f $bytes[$j]) }
        [void]$sb.AppendLine("    " + ($chunk -join ", ") + ",")
    }
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine(("static const unsigned int ${arrayName}_len = {0};" -f $bytes.Length))
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($headerPath, $sb.ToString(), $utf8NoBom)
    Write-Host "OK: $className -> $($bytes.Length) bytes -> $headerPath"
}

Write-ClassHeader "FrenchifyRenderGlobal" (Join-Path $lunarHeaders "FrenchifyRenderGlobalLunar.h") "FrenchifyRenderGlobalLunar_class" "Auto-generated Lunar bytecode for FrenchifyRenderGlobal"
Write-ClassHeader "FrenchifyHitboxInstaller" (Join-Path $lunarHeaders "FrenchifyHitboxInstallerLunar.h") "FrenchifyHitboxInstallerLunar_class" "Auto-generated Lunar bytecode for FrenchifyHitboxInstaller"
Write-ClassHeader "FrenchifyClientPump" (Join-Path $lunarHeaders "FrenchifyClientPumpLunar.h") "FrenchifyClientPumpLunar_class" "Auto-generated Lunar bytecode for FrenchifyClientPump"
Copy-Item (Join-Path $lunarHeaders "FrenchifyRenderGlobalLunar.h") (Join-Path $releaseHeaders "FrenchifyRenderGlobalLunar.h") -Force
Copy-Item (Join-Path $lunarHeaders "FrenchifyHitboxInstallerLunar.h") (Join-Path $releaseHeaders "FrenchifyHitboxInstallerLunar.h") -Force
Copy-Item (Join-Path $lunarHeaders "FrenchifyClientPumpLunar.h") (Join-Path $releaseHeaders "FrenchifyClientPumpLunar.h") -Force
