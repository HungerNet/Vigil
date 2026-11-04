# Regenerate java_src/lunar_headers/*Lunar.h from *Lunar.java (MCP bytecode only)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$stubs = Join-Path $root "java_src\stubs_lunar"
$headers = Join-Path $root "java_src\lunar_headers"

function Write-ClassHeader($classFile, $headerPath, $arrayName, $comment) {
    if (-not (Test-Path $classFile)) { throw "Missing class: $classFile" }
    $bytes = [System.IO.File]::ReadAllBytes($classFile)
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("#pragma once")
    [void]$sb.AppendLine("// Auto-generated Lunar bytecode ($comment)")
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
    Write-Host "OK: $arrayName ($($bytes.Length) bytes) -> $headerPath"
}

New-Item -ItemType Directory -Force -Path $headers | Out-Null

python (Join-Path $root "scripts\generate_lunar_java.py")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# GUI
$guiOut = Join-Path $root "build\gui_lunar_compile"
New-Item -ItemType Directory -Force -Path $guiOut | Out-Null
Copy-Item (Join-Path $root "java_src\VigilGui4Lunar.java") (Join-Path $guiOut "VigilGui4.java") -Force
javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $guiOut (Join-Path $guiOut "VigilGui4.java")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-ClassHeader (Join-Path $guiOut "VigilGui4.class") (Join-Path $headers "VigilGuiLunar.h") "VigilGuiLunar_class" "VigilGui4 MCP"

# Tab overlay
$tabOut = Join-Path $root "build\tab_overlay_lunar_compile"
New-Item -ItemType Directory -Force -Path $tabOut | Out-Null
Copy-Item (Join-Path $root "java_src\VigilTabOverlayNativeLunar.java") (Join-Path $tabOut "VigilTabOverlayNative.java") -Force
javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $tabOut (Join-Path $tabOut "VigilTabOverlayNative.java")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-ClassHeader (Join-Path $tabOut "VigilTabOverlayNative.class") (Join-Path $headers "VigilTabOverlayNativeLunar.h") "VigilTabOverlayNativeLunar_class" "VigilTabOverlayNative MCP"

# Hitboxes
$hbOut = Join-Path $root "build\hitbox_lunar_compile"
New-Item -ItemType Directory -Force -Path $hbOut | Out-Null
foreach ($name in @("VigilRenderGlobalLunar.java", "VigilHitboxInstallerLunar.java")) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($name) -replace "Lunar$", ""
    Copy-Item (Join-Path $root "java_src\$name") (Join-Path $hbOut "$base.java") -Force
    javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $hbOut (Join-Path $hbOut "$base.java")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
Write-ClassHeader (Join-Path $hbOut "VigilRenderGlobal.class") (Join-Path $headers "VigilRenderGlobalLunar.h") "VigilRenderGlobalLunar_class" "VigilRenderGlobal MCP"
Write-ClassHeader (Join-Path $hbOut "VigilHitboxInstaller.class") (Join-Path $headers "VigilHitboxInstallerLunar.h") "VigilHitboxInstallerLunar_class" "VigilHitboxInstaller MCP"

# Client pump
$pumpOut = Join-Path $root "build\client_pump_lunar_compile"
New-Item -ItemType Directory -Force -Path $pumpOut | Out-Null
Copy-Item (Join-Path $root "java_src\VigilClientPumpLunar.java") (Join-Path $pumpOut "VigilClientPump.java") -Force
javac -source 1.8 -target 1.8 -encoding UTF-8 -d $pumpOut (Join-Path $pumpOut "VigilClientPump.java")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-ClassHeader (Join-Path $pumpOut "VigilClientPump.class") (Join-Path $headers "VigilClientPumpLunar.h") "VigilClientPumpLunar_class" "VigilClientPump MCP"

# Session HUD
$sessionOut = Join-Path $root "build\session_lunar_compile"
New-Item -ItemType Directory -Force -Path $sessionOut | Out-Null
Copy-Item (Join-Path $root "java_src\VigilIngameGuiLunar.java") (Join-Path $sessionOut "VigilIngameGui.java") -Force
Copy-Item (Join-Path $root "java_src\VigilSessionHudLunar.java") (Join-Path $sessionOut "VigilSessionHud.java") -Force
Copy-Item (Join-Path $root "java_src\VigilSessionHudInstallerLunar.java") (Join-Path $sessionOut "VigilSessionHudInstaller.java") -Force
javac -source 1.8 -target 1.8 -encoding UTF-8 -cp $stubs -d $sessionOut `
    (Join-Path $sessionOut "VigilSessionHud.java") `
    (Join-Path $sessionOut "VigilIngameGui.java") `
    (Join-Path $sessionOut "VigilSessionHudInstaller.java")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-ClassHeader (Join-Path $sessionOut "VigilIngameGui.class") (Join-Path $headers "VigilIngameGuiLunar.h") "VigilIngameGuiLunar_class" "VigilIngameGui MCP"
Write-ClassHeader (Join-Path $sessionOut "VigilSessionHud.class") (Join-Path $headers "VigilSessionHudLunar.h") "VigilSessionHudLunar_class" "VigilSessionHud MCP"
Write-ClassHeader (Join-Path $sessionOut "VigilSessionHudInstaller.class") (Join-Path $headers "VigilSessionHudInstallerLunar.h") "VigilSessionHudInstallerLunar_class" "VigilSessionHudInstaller MCP"

Write-Host "All lunar headers regenerated in $headers"
