package net.minecraft.client.renderer;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.client.renderer.culling.ICamera;
import net.minecraft.entity.Entity;

public class RenderGlobal {
    protected Minecraft mc;
    public WorldClient theWorld;

    public RenderGlobal(Minecraft mc) {
        this.mc = mc;
    }

    public void renderEntities(Entity entity, ICamera camera, float partialTicks) {}
    public void renderSky(float partialTicks, int pass) {}
    public void renderSky(WorldRenderer worldRendererIn, float partialTicks, boolean flag) {}
}
