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

import java.nio.ByteBuffer;

/**
 * CpuImage contains CPU-accessible YUV image data from an ARCore camera frame, along with
 * camera pose, intrinsics, and timing information.
 * <p>
 * This is provided when {@code enableCpuImages=true} in
 * {@link ViroViewARCore#setPreViewportFrameListener(FrameTapListener, boolean)}.
 * <p>
 * The image format is YUV_420_888 with separate Y, U, and V planes. Each plane is provided
 * as a {@link ByteBuffer} backed by native memory.
 * <p>
 * <b>Important:</b> The ByteBuffers are valid only during the
 * {@link FrameTapListener#onCpuImageFrame(CpuImage)} callback. If you need the data beyond
 * the callback, you MUST copy it to your own buffers.
 */
public final class CpuImage {

    // Timing
    /**
     * Frame timestamp from ARCore in nanoseconds. This matches the timestamp from the
     * corresponding {@link TextureInfo}.
     */
    public final long timestampNs;

    // YUV Plane Data (YUV_420_888 format)
    /**
     * Y plane (luminance) data. This is a direct ByteBuffer backed by native memory.
     * <p>
     * Size: {@code height * yRowStride} bytes.
     * <p>
     * <b>Lifetime:</b> Valid only during {@link FrameTapListener#onCpuImageFrame} callback.
     */
    public final ByteBuffer yPlane;

    /**
     * U plane (Cb chrominance) data. This is a direct ByteBuffer backed by native memory.
     * <p>
     * Size: {@code (height/2) * uvRowStride} bytes (subsampled by 2x2).
     * <p>
     * <b>Lifetime:</b> Valid only during {@link FrameTapListener#onCpuImageFrame} callback.
     */
    public final ByteBuffer uPlane;

    /**
     * V plane (Cr chrominance) data. This is a direct ByteBuffer backed by native memory.
     * <p>
     * Size: {@code (height/2) * uvRowStride} bytes (subsampled by 2x2).
     * <p>
     * <b>Lifetime:</b> Valid only during {@link FrameTapListener#onCpuImageFrame} callback.
     */
    public final ByteBuffer vPlane;

    /**
     * Row stride (bytes per row) for the Y plane. May include padding; use this value when
     * accessing rows, not {@code width}.
     */
    public final int yRowStride;

    /**
     * Row stride (bytes per row) for the U and V planes. May include padding.
     */
    public final int uvRowStride;

    /**
     * Pixel stride (bytes between adjacent samples) for the U and V planes.
     * Typically 1 for planar formats or 2 for semi-planar (interleaved UV).
     */
    public final int uvPixelStride;

    // Dimensions
    /**
     * Image width in pixels. This is the full camera resolution width.
     */
    public final int width;

    /**
     * Image height in pixels. This is the full camera resolution height.
     */
    public final int height;

    // Camera Pose (same as TextureInfo)
    /**
     * 4x4 view matrix (world-to-camera transform) in row-major order.
     */
    public final float[] viewMatrix;

    /**
     * 4x4 projection matrix in row-major order.
     */
    public final float[] projectionMatrix;

    /**
     * Horizontal focal length (fx) in pixels.
     */
    public final float focalLengthX;

    /**
     * Vertical focal length (fy) in pixels.
     */
    public final float focalLengthY;

    /**
     * Horizontal principal point (cx) in pixels.
     */
    public final float principalPointX;

    /**
     * Vertical principal point (cy) in pixels.
     */
    public final float principalPointY;

    /**
     * Display rotation (Surface.ROTATION_0/90/180/270).
     */
    public final int displayRotation;

    /**
     * Package-private constructor. Instances are created internally by ViroCore.
     */
    CpuImage(long timestampNs, ByteBuffer y, ByteBuffer u, ByteBuffer v,
             int yStride, int uvStride, int uvPixelStride, int width, int height,
             float[] view, float[] projection, float fx, float fy, float cx, float cy,
             int rotation) {
        this.timestampNs = timestampNs;
        this.yPlane = y;
        this.uPlane = u;
        this.vPlane = v;
        this.yRowStride = yStride;
        this.uvRowStride = uvStride;
        this.uvPixelStride = uvPixelStride;
        this.width = width;
        this.height = height;
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
        return String.format("CpuImage{timestamp=%d, size=%dx%d, yStride=%d, uvStride=%d, uvPixelStride=%d, rotation=%d}",
                timestampNs, width, height, yRowStride, uvRowStride, uvPixelStride, displayRotation);
    }
}