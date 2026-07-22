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
 * FrameTapListener receives callbacks for each ARCore camera frame before it is rendered to the
 * viewport. This provides access to the full-resolution camera texture and optional CPU image data,
 * along with pose and intrinsics information, for external processing (e.g., WebRTC streaming,
 * computer vision).
 * <p>
 * This listener is installed via {@link ViroViewARCore#setPreViewportFrameListener(FrameTapListener, boolean)}.
 * <p>
 * <b>Threading:</b> All callbacks are invoked on a dedicated background thread, NOT the GL thread
 * or main thread. This allows CPU-intensive processing without blocking AR rendering.
 * <p>
 * <b>Performance:</b> Callbacks should complete quickly (target <5ms for texture, <10ms for CPU)
 * to avoid frame drops. For longer processing, copy the data and process on a separate thread.
 */
public interface FrameTapListener {

    /**
     * Called when a new texture frame is available from ARCore.
     * <p>
     * The provided {@link TextureInfo} contains the OpenGL ES external texture ID, transform matrix,
     * camera pose, intrinsics, and timing information.
     * <p>
     * <b>Threading:</b> Invoked on a dedicated background thread (NOT GL thread, NOT main thread).
     * <p>
     * <b>Lifetime:</b> All data in {@code info} is valid until this method returns. The OpenGL
     * texture ID ({@code info.oesTextureId}) is owned by ARCore and must NOT be stored or used
     * after this method returns. If you need the texture data, copy it to your own GL texture
     * or framebuffer within this callback.
     * <p>
     * <b>Performance:</b> This callback should complete quickly (target <5ms) to maintain 30fps.
     * Avoid blocking operations.
     *
     * @param info Frame data including texture ID, transform, pose, and intrinsics. Valid only
     *             during the callback execution.
     */
    void onTextureFrame(TextureInfo info);

    /**
     * Called when CPU image data is available (optional path, requires {@code enableCpuImages=true}).
     * <p>
     * The provided {@link CpuImage} contains YUV420 plane data from ARCore, along with pose,
     * intrinsics, and timing information.
     * <p>
     * <b>Threading:</b> Invoked on the same dedicated background thread as {@link #onTextureFrame}.
     * <p>
     * <b>Lifetime:</b> The {@link java.nio.ByteBuffer} instances in {@code image} are backed by
     * native memory and are valid only until this method returns. If you need the data beyond
     * this callback, you MUST copy it to your own buffers.
     * <p>
     * <b>Performance:</b> CPU image processing is inherently more expensive than texture processing.
     * This callback should complete quickly (target <10ms) but may be slower than texture path.
     *
     * @param image Frame data with YUV planes, pose, and intrinsics. ByteBuffers are valid only
     *              during the callback execution.
     */
    void onCpuImageFrame(CpuImage image);
}