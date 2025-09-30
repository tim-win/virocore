# ViroCore (Android) Pre-Viewport Frame Tap API (Design)

This complements the Viro (React) API, implemented at the renderer level.

## Renderer responsibilities
- Acquire ARCore `Frame` each render tick; before viewport composition:
  - Build `TextureInfo` from ARCore camera texture + transform.
  - When enabled, build `CpuImage` using `Frame.acquireCameraImage()`.
  - Attach pose/intrinsics per frame.
- Dispatch to an optional `FrameTapListener` registered from Viro.

## Suggested classes (Java-facing)
- `com.viromedia.viro.ar.TextureInfo` and `CpuImage` as in the Viro doc.
- `FrameTapListener` interface as in the Viro doc.

## ARCore specifics
- Use DisplayRotationHelper-equivalent to compute `texTransform4x4` and projection.
- Provide camera intrinsics via `getImageIntrinsics()` (fx, fy, cx, cy in px).
- Provide pose via `frame.getCamera().getPose()` (convert to 4x4 row/col-major consistently).

## Session config for CPU images
- Only enable CPU images when a listener is present and requests CPU path; disable on removal.
- Prefer texture path for performance; CPU path gated behind flag.

## Threading and lifetime
- If invoking on GL thread, ensure buffers remain valid until listener returns.
- For CPU images, close `Image` after copying planes to direct ByteBuffers.

## Back-Compat
- No changes to existing screenshot/recording or CameraImageListener behavior.
