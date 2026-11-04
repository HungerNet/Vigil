#!/usr/bin/env python3
"""Generate VigilGui4Lunar.java from VigilGui4.java (MCP names for Lunar)."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "java_src" / "VigilGui4.java"
OUT = ROOT / "java_src" / "VigilGui4Lunar.java"

REPLACEMENTS = [
    ("public class VigilGui4 extends axu {",
     "import net.minecraft.client.Minecraft;\n"
     "import net.minecraft.client.gui.FontRenderer;\n"
     "import net.minecraft.client.gui.Gui;\n"
     "import net.minecraft.client.gui.GuiScreen;\n"
     "public class VigilGui4 extends GuiScreen {"),
    ("private static avn cachedFontRenderer = null;", "private static FontRenderer cachedFontRenderer = null;"),
    ("    private avn resolveFontRenderer() {\n"
     "        if (cachedFontRenderer != null) return cachedFontRenderer;\n"
     "        try {\n"
     "            Class mcCls = Class.forName(\"ave\");\n"
     "            Method getMc = mcCls.getMethod(\"A\");\n"
     "            Object mc = getMc.invoke(null);\n"
     "            if (mc != null) {\n"
     "                Field[] fields = mcCls.getDeclaredFields();\n"
     "                for (int i = 0; i < fields.length; i++) {\n"
     "                    Field f = fields[i];\n"
     "                    if (f.getType() != null && \"avn\".equals(f.getType().getName())) {\n"
     "                        f.setAccessible(true);\n"
     "                        Object v = f.get(mc);\n"
     "                        if (v instanceof avn) {\n"
     "                            cachedFontRenderer = (avn)v;\n"
     "                            return cachedFontRenderer;\n"
     "                        }\n"
     "                    }\n"
     "                }\n"
     "            }\n"
     "        } catch (Throwable ignored) {\n"
     "        }\n"
     "        return null;\n"
     "    }",
     "    private FontRenderer resolveFontRenderer() {\n"
     "        if (cachedFontRenderer != null) return cachedFontRenderer;\n"
     "        try {\n"
     "            Minecraft mc = Minecraft.getMinecraft();\n"
     "            if (mc != null && mc.fontRendererObj != null) {\n"
     "                cachedFontRenderer = mc.fontRendererObj;\n"
     "                return cachedFontRenderer;\n"
     "            }\n"
     "        } catch (Throwable ignored) {\n"
     "        }\n"
     "        return null;\n"
     "    }"),
    ("        avn fr = resolveFontRenderer();", "        FontRenderer fr = resolveFontRenderer();"),
    ("        if (fr != null) this.c(fr, text, x, y, color);", "        if (fr != null) this.drawString(fr, text, x, y, color);"),
    ("        if (fr != null) this.a(fr, text, x, y, color);", "        if (fr != null) this.drawCenteredString(fr, text, x, y, color);"),
    ("    private void closeScreenSafe() {\n"
     "        try {\n"
     "            Class mcCls = Class.forName(\"ave\");\n"
     "            Method getMc = mcCls.getMethod(\"A\");\n"
     "            Object mc = getMc.invoke(null);\n"
     "            if (mc == null) return;\n"
     "            Method[] methods = mcCls.getDeclaredMethods();\n"
     "            for (int i = 0; i < methods.length; i++) {\n"
     "                Method m = methods[i];\n"
     "                Class[] pt = m.getParameterTypes();\n"
     "                if (pt != null && pt.length == 1 && pt[0] != null && \"axu\".equals(pt[0].getName())) {\n"
     "                    m.setAccessible(true);\n"
     "                    m.invoke(mc, new Object[] { null });\n"
     "                    return;\n"
     "                }\n"
     "            }\n"
     "        } catch (Throwable ignored) {\n"
     "        }\n"
     "    }",
     "    private void closeScreenSafe() {\n"
     "        try {\n"
     "            Minecraft mc = Minecraft.getMinecraft();\n"
     "            if (mc != null) mc.displayGuiScreen(null);\n"
     "        } catch (Throwable ignored) {\n"
     "        }\n"
     "    }"),
    ("            Object mc = this.j;", "            Object mc = this.mc;"),
    ("    public void b() {\n        this.n.clear();",
     "    public void initGui() {\n        this.buttonList.clear();"),
    ("        winX = this.l / 2 - winW / 2;\n        winY = this.m / 2 - winH / 2;",
     "        winX = this.width / 2 - winW / 2;\n        winY = this.height / 2 - winH / 2;"),
    ("    public void k() {\n        super.k();",
     "    public void handleMouseInput() {\n        super.handleMouseInput();"),
    ("    protected void a(int mouseX, int mouseY, int mouseButton) {\n        super.a(mouseX, mouseY, mouseButton);",
     "    protected void mouseClicked(int mouseX, int mouseY, int mouseButton) {\n        super.mouseClicked(mouseX, mouseY, mouseButton);"),
    ("    protected void a(char typedChar, int keyCode) {",
     "    protected void keyTyped(char typedChar, int keyCode) {"),
    ("    public void a(int mouseX, int mouseY, float f) {\n\n        if (draggingHitboxSlider && selectedTheme == 2 && coloredHitboxes && Mouse.isButtonDown(0)) {\n            hitboxThickness = hitboxThicknessFromMouse(hitboxSliderTrackX, hitboxSliderTrackW, mouseX);\n        } else if (!Mouse.isButtonDown(0)) {\n            draggingHitboxSlider = false;\n        }\n\n        int cfgTextPrimary = 0xFFFFFFFF;",
     "    public void drawScreen(int mouseX, int mouseY, float f) {\n\n        if (draggingHitboxSlider && selectedTheme == 2 && coloredHitboxes && Mouse.isButtonDown(0)) {\n            hitboxThickness = hitboxThicknessFromMouse(hitboxSliderTrackX, hitboxSliderTrackW, mouseX);\n        } else if (!Mouse.isButtonDown(0)) {\n            draggingHitboxSlider = false;\n        }\n\n        int cfgTextPrimary = 0xFFFFFFFF;"),
    ("        this.c();", "        this.drawDefaultBackground();"),
    ("        super.a(mouseX, mouseY, f);", "        super.drawScreen(mouseX, mouseY, f);"),
]


def main() -> None:
    text = SRC.read_text(encoding="utf-8")
    if "public class VigilGui4 extends GuiScreen {" in text:
        OUT.write_text(text, encoding="utf-8")
        print(f"Wrote {OUT}")
        return

    for old, new in REPLACEMENTS:
        if old not in text:
            raise SystemExit(f"Missing expected block: {old[:60]}...")
        text = text.replace(old, new, 1)
    text = text.replace("avp.a(", "Gui.drawRect(")
    text = text.replace("avn fr", "FontRenderer fr")
    OUT.write_text(text, encoding="utf-8")
    print(f"Wrote {OUT}")


if __name__ == "__main__":
    main()
