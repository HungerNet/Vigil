import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.RenderGlobal;
import net.minecraft.client.renderer.culling.ICamera;
import net.minecraft.entity.Entity;

// Vigil — RenderGlobal subclass installed in place of Minecraft#renderGlobal. After vanilla
// entities are drawn it calls into Vigil.dll, which draws Suspect Highlight outlines in world space.
public class VigilRenderGlobal extends RenderGlobal {
    public VigilRenderGlobal(Minecraft mc) {
        super(mc);
    }

    public void renderEntities(Entity renderViewEntity, ICamera camera, float partialTicks) {
        super.renderEntities(renderViewEntity, camera, partialTicks);
        nativeAfterRenderEntities();
    }

    private static native void nativeAfterRenderEntities();
}
