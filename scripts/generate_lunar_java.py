#!/usr/bin/env python3
"""Generate *Lunar.java from vanilla java_src using MCP names only (no logic changes)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

PARENT = Path(__file__).resolve().parents[1]
JAVA = PARENT / "java_src"


def write_tab_overlay() -> None:
    src = JAVA / "VigilTabOverlayNative.java"
    out = JAVA / "VigilTabOverlayNativeLunar.java"
    text = src.read_text(encoding="utf-8")
    header = (
        "import net.minecraft.client.Minecraft;\n"
        "import net.minecraft.client.gui.GuiIngame;\n"
        "import net.minecraft.client.gui.GuiPlayerTabOverlay;\n"
        "import net.minecraft.scoreboard.Scoreboard;\n"
        "import net.minecraft.scoreboard.ScoreObjective;\n\n"
    )
    text = text.replace(
        "public class VigilTabOverlayNative extends awh {",
        header + "public class VigilTabOverlayNative extends GuiPlayerTabOverlay {",
    )
    text = text.replace("VigilTabOverlayNative(ave mc, avo guiIngame)", "VigilTabOverlayNative(Minecraft mc, GuiIngame guiIngame)")
    text = text.replace("public void a(int width, auo scoreboard, auk objective)", "public void renderPlayerlist(int width, Scoreboard scoreboard, ScoreObjective objective)")
    text = text.replace("super.a(width, scoreboard, objective)", "super.renderPlayerlist(width, scoreboard, objective)")
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out.relative_to(PARENT)}")


def write_render_global() -> None:
    src = JAVA / "VigilRenderGlobal.java"
    out = JAVA / "VigilRenderGlobalLunar.java"
    text = src.read_text(encoding="utf-8")
    header = (
        "import net.minecraft.client.Minecraft;\n"
        "import net.minecraft.client.renderer.RenderGlobal;\n"
        "import net.minecraft.client.renderer.culling.ICamera;\n"
        "import net.minecraft.entity.Entity;\n\n"
    )
    text = text.replace(
        "public class VigilRenderGlobal extends bfr {",
        header + "public class VigilRenderGlobal extends RenderGlobal {",
    )
    text = text.replace("VigilRenderGlobal(ave mc)", "VigilRenderGlobal(Minecraft mc)")
    text = text.replace("public void a(pk entity, bia camera, float partialTicks)", "public void renderEntities(Entity entity, ICamera camera, float partialTicks)")
    text = text.replace("super.a(entity, camera, partialTicks)", "super.renderEntities(entity, camera, partialTicks)")
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out.relative_to(PARENT)}")


def write_hitbox_installer() -> None:
    src = JAVA / "VigilHitboxInstaller.java"
    out = JAVA / "VigilHitboxInstallerLunar.java"
    text = src.read_text(encoding="utf-8")
    header = (
        "import java.lang.reflect.Field;\n"
        "import java.lang.reflect.Modifier;\n"
        "import net.minecraft.client.renderer.RenderGlobal;\n\n"
    )
    # Drop duplicate imports from vanilla header block.
    lines = text.splitlines()
    body_start = 0
    for i, line in enumerate(lines):
        if line.startswith("public class VigilHitboxInstaller"):
            body_start = i
            break
    text = header + "\n".join(lines[body_start:]) + "\n"
    text = text.replace("static void copyBfrFields(bfr from, bfr to)", "static void copyRenderGlobalFields(RenderGlobal from, RenderGlobal to)")
    text = text.replace("for (Class<?> c = bfr.class;", "for (Class<?> c = RenderGlobal.class;")
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out.relative_to(PARENT)}")


def write_gui() -> None:
    import subprocess

    maker = PARENT / "scripts" / "make_lunar_gui.py"
    subprocess.run([sys.executable, str(maker)], check=True)


def write_session_ingame_gui() -> None:
    src = JAVA / "VigilIngameGui.java"
    out = JAVA / "VigilIngameGuiLunar.java"
    text = src.read_text(encoding="utf-8")
    header = (
        "import net.minecraft.client.Minecraft;\n"
        "import net.minecraft.client.gui.GuiIngame;\n\n"
    )
    lines = text.splitlines()
    body_start = 0
    for i, line in enumerate(lines):
        if line.startswith("public class VigilIngameGui"):
            body_start = i
            break
    text = header + "\n".join(lines[body_start:]) + "\n"
    text = text.replace("extends avo", "extends GuiIngame")
    text = text.replace("private final ave mcInstance", "private final Minecraft mcInstance")
    text = text.replace("VigilIngameGui(ave mc)", "VigilIngameGui(Minecraft mc)")
    text = text.replace("public void a(float partialTicks)", "public void renderGameOverlay(float partialTicks)")
    text = text.replace("super.a(partialTicks)", "super.renderGameOverlay(partialTicks)")
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out.relative_to(PARENT)}")


def write_session_hud() -> None:
    src = JAVA / "VigilSessionHud.java"
    out = JAVA / "VigilSessionHudLunar.java"
    text = src.read_text(encoding="utf-8")
    header = (
        "import net.minecraft.client.Minecraft;\n"
        "import net.minecraft.client.gui.FontRenderer;\n"
        "import net.minecraft.client.gui.Gui;\n"
        "import net.minecraft.client.renderer.GlStateManager;\n\n"
    )
    lines = text.splitlines()
    body_start = 0
    for i, line in enumerate(lines):
        if line.startswith("public final class VigilSessionHud"):
            body_start = i
            break
    text = header + "\n".join(lines[body_start:]) + "\n"
    text = text.replace("public static void draw(ave mc)", "public static void draw(Minecraft mc)")
    text = text.replace("avn fr = mc.k", "FontRenderer fr = mc.fontRendererObj")
    text = text.replace("int w = fr.a(row)", "int w = fr.getStringWidth(row)")
    text = text.replace("bfl.E()", "GlStateManager.pushMatrix()")
    text = text.replace("bfl.F()", "GlStateManager.popMatrix()")
    text = text.replace("bfl.b(", "GlStateManager.translate(")
    text = text.replace("bfl.a(", "GlStateManager.scale(")
    text = text.replace(
        "avp.a(0, 0, (int) baseWidth, (int) baseHeight, alpha << 24)",
        "Gui.drawRect(0, 0, (int) baseWidth, (int) baseHeight, alpha << 24)",
    )
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out.relative_to(PARENT)}")


def write_session_hud_installer() -> None:
    src = JAVA / "VigilSessionHudInstaller.java"
    out = JAVA / "VigilSessionHudInstallerLunar.java"
    text = src.read_text(encoding="utf-8")
    header = (
        "import java.lang.reflect.Field;\n"
        "import java.lang.reflect.Modifier;\n"
        "import net.minecraft.client.gui.GuiIngame;\n\n"
    )
    lines = text.splitlines()
    body_start = 0
    for i, line in enumerate(lines):
        if line.startswith("public class VigilSessionHudInstaller"):
            body_start = i
            break
    text = header + "\n".join(lines[body_start:]) + "\n"
    text = text.replace(
        "static void copyAvoFields(avo from, avo to)",
        "static void copyGuiIngameFields(GuiIngame from, GuiIngame to)",
    )
    text = text.replace("for (Class<?> c = avo.class;", "for (Class<?> c = GuiIngame.class;")
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out.relative_to(PARENT)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gui-only", action="store_true")
    args = parser.parse_args()

    if args.gui_only:
        write_gui()
        return 0

    write_tab_overlay()
    write_render_global()
    write_hitbox_installer()
    write_session_ingame_gui()
    write_session_hud()
    write_session_hud_installer()
    write_gui()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
