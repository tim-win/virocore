# ViroCore Frame Tap API: Requirements Specification

**Version**: 1.0
**Date**: 2025-09-29
**Status**: Step 1 - Requirements Definition

---

## 1. Overview

This specification defines the exact API surface for exposing ARCore camera frames to external consumers (e.g., WebRTC) without interfering with ViroCore's AR rendering pipeline.

### Goals
- Provide uncropped, full-resolution camera frames (texture and/or CPU paths)
- Include pose, intrinsics, and timing data for each frame
- Zero impact on AR tracking when listener not registered
- Thread-safe, non-blocking design

---

## 2. API Surface

### 2.1 FrameTapListener Interface

**Package**: `com.viro.core`

```java
public interface FrameTapListener {

    /**
     * Called when a new texture frame is available from ARCore.
     *
     * Threading: Invoked on a dedicated background thread (NOT GL thread, NOT main thread)
     * Lifetime: All data in TextureInfo is valid until this method returns
     * Performance: Must return quickly (<5ms) to avoid frame drops
     *
     * @param info Frame data including texture, pose, and intrinsics
     */
    void onTextureFrame(TextureInfo info);

    /**
     * Called when CPU image data is available (optional path, requires enableCpuImages).
     *
     * Threading: Invoked on same background thread as onTextureFrame
     * Lifetime: ByteBuffers are valid until this method returns; copy if needed
     * Performance: YUV processing is CPU-intensive; budget accordingly
     *
     * @param image Frame data with YUV planes, pose, and intrinsics
     */
    void onCpuImageFrame(CpuImage image);
}
```

---

### 2.2 TextureInfo Class

**Package**: `com.viro.core`

```java
public final class TextureInfo {

    // Timing
    public final long timestampNs;           // Frame timestamp from ARCore (nanoseconds)

    // Texture Data
    public final int oesTextureId;           // OpenGL ES external texture ID (GL_TEXTURE_EXTERNAL_OES)
    public final float[] texTransform;       // 4x4 column-major transform matrix (from ARCore)
    public final int textureWidth;           // Native camera texture width (e.g., 1920)
    public final int textureHeight;          // Native camera texture height (e.g., 1080)

    // Camera Pose (camera-to-world transform)
    public final float[] viewMatrix;         // 4x4 row-major view matrix (world-to-camera)
    public final float[] projectionMatrix;   // 4x4 row-major projection matrix

    // Camera Intrinsics
    public final float focalLengthX;         // fx in pixels
    public final float focalLengthY;         // fy in pixels
    public final float principalPointX;      // cx in pixels
    public final float principalPointY;      // cy in pixels

    // Display Orientation
    public final int displayRotation;        // Surface.ROTATION_0/90/180/270

    /**
     * Constructor (package-private, created by ViroCore internals)
     */
    TextureInfo(long timestampNs, int oesTextureId, float[] texTransform,
                int width, int height, float[] view, float[] projection,
                float fx, float fy, float cx, float cy, int rotation) {
        this.timestampNs = timestampNs;
        this.oesTextureId = oesTextureId;
        this.texTransform = texTransform;
        this.textureWidth = width;
        this.textureHeight = height;
        this.viewMatrix = view;
        this.projectionMatrix = projection;
        this.focalLengthX = fx;
        this.focalLengthY = fy;
        this.principalPointX = cx;
        this.principalPointY = cy;
        this.displayRotation = rotation;
    }
}
```

---

### 2.3 CpuImage Class

**Package**: `com.viro.core`

```java
public final class CpuImage {

    // Timing
    public final long timestampNs;

    // YUV Plane Data (from ARCore Image.Plane)
    public final ByteBuffer yPlane;          // Y plane (luminance)
    public final ByteBuffer uPlane;          // U plane (Cb chrominance)
    public final ByteBuffer vPlane;          // V plane (Cr chrominance)
    public final int yRowStride;             // Bytes per row in Y plane
    public final int uvRowStride;            // Bytes per row in U/V planes
    public final int uvPixelStride;          // Bytes between adjacent UV samples

    // Dimensions
    public final int width;                  // Image width in pixels
    public final int height;                 // Image height in pixels

    // Camera Pose & Intrinsics (same as TextureInfo)
    public final float[] viewMatrix;
    public final float[] projectionMatrix;
    public final float focalLengthX;
    public final float focalLengthY;
    public final float principalPointX;
    public final float principalPointY;
    public final int displayRotation;

    /**
     * Constructor (package-private)
     */
    CpuImage(long timestampNs, ByteBuffer y, ByteBuffer u, ByteBuffer v,
             int yStride, int uvStride, int uvPixelStride, int width, int height,
             float[] view, float[] projection, float fx, float fy, float cx, float cy,
             int rotation) {
        // Assignment logic
    }
}
```

---

### 2.4 ViroViewARCore Public Methods

Add to existing `ViroViewARCore` class:

```java
public class ViroViewARCore extends ViroView {

    /**
     * Register a listener to receive pre-viewport camera frames.
     *
     * @param listener The FrameTapListener to receive callbacks
     * @param enableCpuImages If true, also deliver CPU YUV images (performance cost)
     *
     * Threading: Safe to call from any thread
     * Lifecycle: Listener persists until clearPreViewportFrameListener() or view destroyed
     */
    public void setPreViewportFrameListener(FrameTapListener listener, boolean enableCpuImages) {
        // Implementation in Step 2
    }

    /**
     * Unregister the frame listener.
     *
     * Threading: Safe to call from any thread
     * Effect: No more callbacks after this returns (may block briefly to ensure clean removal)
     */
    public void clearPreViewportFrameListener() {
        // Implementation in Step 2
    }

    /**
     * Check if CPU images are supported on this device.
     *
     * @return true if ARCore supports Image.acquireCameraImage() on this device
     */
    public boolean isCpuImageSupported() {
        // Query ARCore capabilities
    }
}
```

---

## 3. ARCore Integration Points

### 3.1 Required ARCore APIs

**Frame Acquisition**:
```java
// In existing render loop (RendererARCore.java or similar)
Frame frame = session.update();

// Extract texture info
int cameraTextureId = session.getCameraTextureId();
float[] texTransform = new float[16];
frame.transformCoordinates2d(...);  // Or equivalent API

// Extract camera intrinsics
CameraIntrinsics intrinsics = frame.getCamera().getTextureIntrinsics();
float fx = intrinsics.getFocalLength()[0];
float fy = intrinsics.getFocalLength()[1];
float cx = intrinsics.getPrincipalPoint()[0];
float cy = intrinsics.getPrincipalPoint()[1];

// Extract pose
Pose cameraPose = frame.getCamera().getPose();
float[] viewMatrix = new float[16];
cameraPose.toMatrix(viewMatrix, 0);  // Camera-to-world; may need inversion

// CPU image (if enabled)
Image cameraImage = frame.acquireCameraImage();  // YUV_420_888 format
Image.Plane[] planes = cameraImage.getPlanes();
```

**Session Configuration**:
```java
// Enable CPU images only when listener requests it
Config config = new Config(session);
config.setUpdateMode(Config.UpdateMode.LATEST_CAMERA_IMAGE);
// CPU images automatically available via acquireCameraImage()
```

### 3.2 Injection Point

**Location**: In the render loop, **immediately after** `session.update()` and **before** rendering to viewport.

**Pseudocode**:
```java
void onDrawFrame(GL10 gl) {
    Frame frame = arSession.update();

    // >>> INJECTION POINT: Dispatch to FrameTapListener here <<<
    if (frameTapListener != null) {
        dispatchFrameToListener(frame);
    }

    // Continue with normal rendering
    renderARScene();
}
```

---

## 4. Threading Model

### 4.1 Requirements

- **Listener callbacks**: Must run on a **dedicated background thread** (not GL thread, not main thread)
- **Reason**: Listener may do CPU-intensive work (YUV conversion, encoding); cannot block rendering
- **Guarantee**: Callbacks are serialized (no concurrent calls for same listener)

### 4.2 Implementation Approach

```java
// Create a single-thread executor for frame callbacks
private ExecutorService frameCallbackExecutor = Executors.newSingleThreadExecutor(
    new ThreadFactory() {
        public Thread newThread(Runnable r) {
            Thread t = new Thread(r, "ViroFrameTap");
            t.setPriority(Thread.NORM_PRIORITY - 1);  // Slightly lower than UI
            return t;
        }
    }
);

// In render loop (GL thread)
void dispatchFrameToListener(Frame frame) {
    // Extract data on GL thread (fast)
    TextureInfo info = buildTextureInfo(frame);

    // Dispatch callback on background thread (async)
    frameCallbackExecutor.execute(() -> {
        listener.onTextureFrame(info);
    });
}
```

### 4.3 Frame Dropping Policy

- If listener callback is still processing when next frame arrives, **drop the old frame** (don't queue)
- Rationale: Streaming use case prefers fresh frames over old buffered frames
- Implementation: Use `AtomicBoolean isProcessing` flag; skip dispatch if true

---

## 5. Memory & Lifetime Management

### 5.1 TextureInfo Lifetime

- `oesTextureId` is owned by ARCore; valid until next `session.update()`
- Listener **must not** store the texture ID across frames
- Arrays (`texTransform`, `viewMatrix`, etc.) are **copied** into TextureInfo; safe to store

### 5.2 CpuImage Lifetime

- `ByteBuffer` instances are backed by `Image.getPlanes()[].getBuffer()`
- ARCore `Image` must remain open during callback; close after callback returns
- Listener **must copy** buffer data if needed beyond callback scope

```java
// In dispatch logic
Image cameraImage = frame.acquireCameraImage();
try {
    CpuImage cpuImage = buildCpuImage(cameraImage, ...);
    listener.onCpuImageFrame(cpuImage);  // ByteBuffers valid here
} finally {
    cameraImage.close();  // Invalidates ByteBuffers
}
```

### 5.3 Listener Lifecycle

- Listener is held as a **strong reference**; consumer must call `clearPreViewportFrameListener()`
- On `clearPreViewportFrameListener()`, block until any in-flight callback completes

---

## 6. Error Handling & Edge Cases

### 6.1 ARCore Frame Acquisition Failures

- If `session.update()` throws or returns `null`, skip frame dispatch (no callback)
- If `acquireCameraImage()` throws (CPU path), skip `onCpuImageFrame()` but still call `onTextureFrame()`

### 6.2 Listener Exceptions

```java
try {
    listener.onTextureFrame(info);
} catch (Exception e) {
    Log.e(TAG, "FrameTapListener threw exception", e);
    // Continue (don't crash render thread)
}
```

### 6.3 Device Compatibility

- CPU images: Check `Config.ImageStabilizationMode` support; not all devices support `acquireCameraImage()`
- Texture path: Should work on all ARCore-supported devices

---

## 7. Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Frame rate | 30 fps | Match ARCore native rate |
| Texture callback overhead | <1 ms | Data extraction on GL thread |
| CPU callback overhead | <10 ms | YUV data copy |
| Frame drops (texture path) | <1% | Even under load |
| Frame drops (CPU path) | <5% | Acceptable for CPU-intensive path |

---

## 8. Backwards Compatibility

- **No API changes** to existing ViroCore code
- Listener is **opt-in**; zero overhead if not registered
- Existing `CameraImageListener` remains unchanged (separate code path)

---

## 9. Testing Strategy (For Step 3)

### Unit Tests
- Listener registration/unregistration
- Thread safety (concurrent register/unregister)
- Frame drop behavior under slow listener

### Integration Tests (Harness App)
- Verify frame rate (30fps)
- Verify resolution matches device camera (1080p+ expected)
- Verify pose data changes as device moves
- Verify texture ID is valid OpenGL texture
- Verify CPU image format (YUV_420_888)

---

## 10. Open Questions / Decisions Needed

1. **Matrix conventions**: ARCore uses column-major; WebRTC may expect row-major. Decide on API convention.
   - **Decision**: Specify clearly in javadoc; consumer handles conversion if needed

2. **Frame dropping**: Should we provide a callback for dropped frames?
   - **Decision**: No; consumer can detect via timestamp gaps if needed

3. **Multiple listeners**: Support multiple simultaneous listeners?
   - **Decision**: No; single listener only for v1

---

## 11. Next Steps (Step 2)

With requirements defined, proceed to:
1. Implement `FrameTapListener`, `TextureInfo`, `CpuImage` classes
2. Add methods to `ViroViewARCore`
3. Integrate into render loop (hook but don't call yet)
4. Ensure code compiles

**Milestone Success Criteria**: Code compiles; API exists but not yet functional.