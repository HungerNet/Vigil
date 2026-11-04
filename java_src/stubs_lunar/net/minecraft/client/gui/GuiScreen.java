package net.minecraft.client.gui;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;

public class GuiScreen extends Gui {
    public Minecraft mc;
    public int width;
    public int height;
    public List buttonList = new ArrayList();

    public void initGui() {}
    public void drawScreen(int mouseX, int mouseY, float partialTicks) {}
    protected void mouseClicked(int mouseX, int mouseY, int mouseButton) {}
    protected void keyTyped(char typedChar, int keyCode) {}
    public void handleMouseInput() {}
    public void drawDefaultBackground() {}

    protected void drawString(FontRenderer fr, String text, int x, int y, int color) {}
    protected void drawCenteredString(FontRenderer fr, String text, int x, int y, int color) {}
}
