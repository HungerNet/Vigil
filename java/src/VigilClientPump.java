// Vigil — schedules native work on the Minecraft client thread.
// Instances are posted with Minecraft#addScheduledTask; run() calls back into Vigil.dll.
public class VigilClientPump implements Runnable {
    public VigilClientPump() {
    }

    public void run() {
        nativeClientPump();
    }

    private static native void nativeClientPump();
}
