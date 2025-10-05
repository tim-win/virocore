//
//  VROFrameTapListener.h
//  ViroRenderer
//
//  Created by Claude Code on 9/29/25.
//  Copyright © 2025 Viro Media. All rights reserved.
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

#ifndef VROFrameTapListener_h
#define VROFrameTapListener_h

#include <memory>
#include <atomic>
#include "VRODefines.h"
#include VRO_C_INCLUDE
#include "VROMatrix4f.h"
#include "VROVector3f.h"

class VROARFrameARCore;
class VROARCameraARCore;

/**
 * VROFrameTapListener bridges C++ ARCore frame data to Java FrameTapListener callbacks.
 * This provides access to full-resolution camera frames before viewport rendering.
 */
class VROFrameTapListener {
public:
    /**
     * Create a new frame tap listener.
     *
     * @param listener_j Java FrameTapListener object (weak global ref will be created)
     * @param enableCpuImages If true, also extract and deliver CPU YUV image data
     * @param env JNI environment
     */
    VROFrameTapListener(VRO_OBJECT listener_j, bool enableCpuImages, VRO_ENV env);
    virtual ~VROFrameTapListener();

    /**
     * Dispatch a frame to the Java listener. This should be called from the render thread
     * after ARCore frame update but before viewport rendering.
     *
     * @param frame The ARCore frame (non-owning pointer)
     * @param cameraTextureId The OpenGL texture ID for the camera frame
     * @param displayRotation The display rotation (Surface.ROTATION_0/90/180/270)
     */
    void dispatchFrame(VROARFrameARCore *frame,
                       int cameraTextureId,
                       int displayRotation);

    /**
     * Check if this listener is still valid (Java object not garbage collected).
     */
    bool isValid() const;

private:
    VRO_OBJECT _listener_j;           // Weak global ref to Java FrameTapListener
    VRO_OBJECT _executor_j;           // Strong global ref to Java ExecutorService
    bool _enableCpuImages;
    std::atomic<bool> _isProcessing;  // Frame drop detection
    int _frameCounter;                // Frame counter for periodic logging

    // Java class/method IDs (cached for performance)
    jclass _textureInfoClass;
    jmethodID _textureInfoConstructor;
    jclass _cpuImageClass;
    jmethodID _cpuImageConstructor;
    jmethodID _onTextureFrameMethod;
    jmethodID _onCpuImageFrameMethod;
    jmethodID _executorExecuteMethod;

    /**
     * Create a Java TextureInfo object from ARCore frame data.
     */
    jobject createTextureInfo(VRO_ENV env,
                              VROARFrameARCore *frame,
                              VROARCameraARCore *camera,
                              int cameraTextureId,
                              int displayRotation);

    /**
     * Create a Java CpuImage object from ARCore frame data (if available).
     */
    jobject createCpuImage(VRO_ENV env,
                           VROARFrameARCore *frame,
                           VROARCameraARCore *camera,
                           int displayRotation);

    /**
     * Extract texture transform matrix from ARCore background texture coordinates.
     */
    void extractTextureTransform(VROVector3f BL, VROVector3f BR, VROVector3f TL, VROVector3f TR,
                                  float outTransform[16]);
};

#endif /* VROFrameTapListener_h */