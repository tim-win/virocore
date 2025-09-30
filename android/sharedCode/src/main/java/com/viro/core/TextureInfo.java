//
//  Copyright (c) 2025-present, ViroMedia, Inc.
//  All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

package com.viro.core;

/**
 * TextureInfo contains information about an ARCore camera frame provided as an OpenGL ES
 * external texture (GL_TEXTURE_EXTERNAL_OES). This includes the texture ID, transform matrix,
 * camera pose, intrinsics, and timing data.
 * <p>
 * Instances of this class are created by ViroCore and passed to
 * {@link FrameTapListener#onTextureFrame(TextureInfo)}.
 * <p>
 * <b>Important:</b> The texture ID is owned by ARCore and is valid only during the callback.
 * Do not store or use the texture ID after the callback returns.
 */
public final class TextureInfo {

    // Timing
    /**
     * Frame timestamp from ARCore in nanoseconds. This is the camera frame capture time and can
     * be used for synchronization with other sensors or streams.
     */
    public final long timestampNs;

    // Texture Data
    /**
     * OpenGL ES external texture ID (GL_TEXTURE_EXTERNAL_OES) containing the camera image.
     * <p>
     * <b>Lifetime:</b> This texture ID is owned by ARCore and is valid only during the callback.
     * You must NOT store this ID or use it after {@link FrameTapListener#onTextureFrame} returns.
     * If you need the texture contents, copy them to your own GL texture or framebuffer within
     * the callback.
     */
    public final int oesTextureId;

    /**
     * 4x4 texture coordinate transform matrix in column-major order. This matrix transforms
     * normalized texture coordinates (0,0) to (1,1) to account for device orientation and
     * camera sensor crop.
     * <p>
     * Use this matrix in your fragment shader to correctly sample the camera texture:
     * <pre>
     * uniform mat4 uTexTransform;
     * varying vec2 vTexCoord;
     * uniform samplerExternalOES uCameraTexture;
     *
     * void main() {
     *     vec2 transformedCoord = (uTexTransform * vec4(vTexCoord, 0, 1)).xy;
     *     gl_FragColor = texture2D(uCameraTexture, transformedCoord);
     * }
     * </pre>
     */
    public final float[] texTransform;

    /**
     * Native camera texture width in pixels. This is the actual size of the camera image,
     * typically 1920x1080 or higher depending on the device.
     */
    public final int textureWidth;

    /**
     * Native camera texture height in pixels.
     */
    public final int textureHeight;

    // Camera Pose
    /**
     * 4x4 view matrix (world-to-camera transform) in row-major order. This represents the
     * camera's pose in the world coordinate system.
     * <p>
     * To get the camera-to-world transform, invert this matrix.
     */
    public final float[] viewMatrix;

    /**
     * 4x4 projection matrix in row-major order. This is the perspective projection matrix
     * for the camera based on its intrinsics and the display aspect ratio.
     */
    public final float[] projectionMatrix;

    // Camera Intrinsics (in pixels, relative to textureWidth x textureHeight)
    /**
     * Horizontal focal length (fx) in pixels. Part of the camera intrinsic matrix.
     */
    public final float focalLengthX;

    /**
     * Vertical focal length (fy) in pixels. Part of the camera intrinsic matrix.
     */
    public final float focalLengthY;

    /**
     * Horizontal principal point (cx) in pixels. Typically near textureWidth/2.
     */
    public final float principalPointX;

    /**
     * Vertical principal point (cy) in pixels. Typically near textureHeight/2.
     */
    public final float principalPointY;

    // Display Orientation
    /**
     * Display rotation relative to the device's natural orientation.
     * Values are from {@link android.view.Surface}: ROTATION_0, ROTATION_90, ROTATION_180, ROTATION_270.
     */
    public final int displayRotation;

    /**
     * Package-private constructor. Instances are created internally by ViroCore.
     * Note: The middle parameters (int, int, float) are reserved for future use.
     */
    TextureInfo(long timestampNs, int oesTextureId, int width, int height,
                float[] texTransform, int reserved1, int reserved2, float reserved3,
                float[] view, float[] projection,
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

    @Override
    public String toString() {
        return String.format("TextureInfo{timestamp=%d, texId=%d, size=%dx%d, rotation=%d}",
                timestampNs, oesTextureId, textureWidth, textureHeight, displayRotation);
    }
}