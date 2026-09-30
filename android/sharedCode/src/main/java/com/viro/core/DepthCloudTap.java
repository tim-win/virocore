package com.viro.core;

/**
 * On-device colored point cloud from ARCore depth (see VRODepthCloudTap.cpp).
 *
 * Points and pose are in ARCore world — the same world Viro's camera
 * transform reports. All methods are safe to call from any thread; they throw
 * UnsatisfiedLinkError if called before the Viro renderer library is loaded,
 * so callers outside ViroCore should guard for that.
 */
public final class DepthCloudTap {
    private DepthCloudTap() {}

    /** Turns ARCore depth + the per-~200ms unproject/voxelize pass on or off. */
    public static native void nativeSetEnabled(boolean enabled);

    /** Clears the voxel set and any undrained points (start of a new scan). */
    public static native void nativeReset();

    /**
     * Moves out every point produced since the last drain, interleaved
     * [x, y, z, argb, ...] where argb is an int stored via
     * Float.intBitsToFloat (decode with Float.floatToRawIntBits).
     */
    public static native float[] nativeDrain();

    /**
     * Latest physical-camera pose, column-major world-from-camera (GL camera:
     * -Z forward, +Y up in SENSOR orientation). Null before the first tracked frame.
     */
    public static native float[] nativeGetPose();

    /** [totalVoxels, ticks, depthWidth, depthHeight]. */
    public static native int[] nativeGetStats();
}
