import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import net.minecraft.client.renderer.RenderGlobal;

// Vigil — swaps Minecraft#renderGlobal for VigilRenderGlobal on the client thread. The native side
// constructs the replacement and calls copyRenderGlobalFields so all render state carries over.
public class VigilHitboxInstaller implements Runnable {
    public VigilHitboxInstaller() {
    }

    public void run() {
        nativeInstallRenderGlobal();
    }

    static void copyRenderGlobalFields(RenderGlobal from, RenderGlobal to) throws IllegalAccessException {
        for (Class<?> c = RenderGlobal.class; c != null && c != Object.class; c = c.getSuperclass()) {
            Field[] fields = c.getDeclaredFields();
            for (Field f : fields) {
                if (Modifier.isStatic(f.getModifiers())) continue;
                f.setAccessible(true);
                f.set(to, f.get(from));
            }
        }
    }

    private static native void nativeInstallRenderGlobal();
}
