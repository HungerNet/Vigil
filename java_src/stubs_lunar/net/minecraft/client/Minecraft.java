package net.minecraft.client;

import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.gui.GuiScreen;

public class Minecraft {
    private static final Minecraft INSTANCE = new Minecraft();
    public FontRenderer fontRendererObj = new FontRenderer();

    public static Minecraft getMinecraft() { return INSTANCE; }
    public void displayGuiScreen(GuiScreen screen) {}
    public Object getSoundHandler() { return null; }
}
