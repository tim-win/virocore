//
//  VROFrameTapListener.cpp
//  ViroRenderer
//
//  Created by Claude Code on 9/29/25.
//  Copyright © 2025 Viro Media. All rights reserved.
//

#include "VROFrameTapListener.h"
#include "VROPlatformUtil.h"
#include "arcore/VROARFrameARCore.h"
#include "arcore/VROARCameraARCore.h"
#include "arcore/VROARSessionARCore.h"
#include "VROLog.h"
#include <android/log.h>

#define FRAME_TAP_TAG "ViroFrameTap"

VROFrameTapListener::VROFrameTapListener(VRO_OBJECT listener_j, bool enableCpuImages, VRO_ENV env) :
    _enableCpuImages(enableCpuImages),
    _isProcessing(false),
    _frameCounter(0) {

    // Create weak global ref to listener (will be checked for validity)
    _listener_j = VRO_NEW_WEAK_GLOBAL_REF(listener_j);

    // Get the ExecutorService from ViroViewARCore (it creates one when listener is set)
    // For now, we'll invoke directly on a background thread. The Java layer handles threading.
    _executor_j = nullptr;

    // Cache Java class and method IDs for performance
    jclass textureInfoClass = env->FindClass("com/viro/core/TextureInfo");
    _textureInfoClass = (jclass) VRO_NEW_GLOBAL_REF(textureInfoClass);
    _textureInfoConstructor = env->GetMethodID(_textureInfoClass, "<init>",
        "(JIII[FIIF[F[FFFFFI)V");
    // (long timestampNs, int oesTextureId, int width, int height,
    //  float[] texTransform, int ignored, int ignored, float ignored,
    //  float[] viewMatrix, float[] projectionMatrix,
    //  float fx, float fy, float cx, float cy, int displayRotation)

    jclass cpuImageClass = env->FindClass("com/viro/core/CpuImage");
    _cpuImageClass = (jclass) VRO_NEW_GLOBAL_REF(cpuImageClass);
    _cpuImageConstructor = env->GetMethodID(_cpuImageClass, "<init>",
        "(JLjava/nio/ByteBuffer;Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;IIIII[F[FFFFFI)V");
    // (long timestampNs, ByteBuffer y, ByteBuffer u, ByteBuffer v,
    //  int yStride, int uvStride, int uvPixelStride, int width, int height,
    //  float[] viewMatrix, float[] projectionMatrix,
    //  float fx, float fy, float cx, float cy, int displayRotation)

    // Get method IDs from the FrameTapListener interface, not the proxy class
    jclass frameTapListenerClass = env->FindClass("com/viro/core/FrameTapListener");
    _onTextureFrameMethod = env->GetMethodID(frameTapListenerClass, "onTextureFrame",
        "(Lcom/viro/core/TextureInfo;)V");
    _onCpuImageFrameMethod = env->GetMethodID(frameTapListenerClass, "onCpuImageFrame",
        "(Lcom/viro/core/CpuImage;)V");

    __android_log_print(ANDROID_LOG_DEBUG, FRAME_TAP_TAG,
        "VROFrameTapListener created (CPU images: %s)", enableCpuImages ? "enabled" : "disabled");
}

VROFrameTapListener::~VROFrameTapListener() {
    VRO_ENV env = VROPlatformGetJNIEnv();

    VRO_DELETE_WEAK_GLOBAL_REF(_listener_j);

    if (_executor_j) {
        VRO_DELETE_GLOBAL_REF(_executor_j);
    }
    if (_textureInfoClass) {
        VRO_DELETE_GLOBAL_REF(_textureInfoClass);
    }
    if (_cpuImageClass) {
        VRO_DELETE_GLOBAL_REF(_cpuImageClass);
    }

    __android_log_print(ANDROID_LOG_DEBUG, FRAME_TAP_TAG, "VROFrameTapListener destroyed");
}

bool VROFrameTapListener::isValid() const {
    // Check if weak global ref is still valid
    VRO_ENV env = VROPlatformGetJNIEnv();
    VRO_OBJECT strongRef = VRO_NEW_LOCAL_REF(_listener_j);
    if (strongRef == nullptr) {
        return false;
    }
    VRO_DELETE_LOCAL_REF(strongRef);
    return true;
}

void VROFrameTapListener::dispatchFrame(VROARFrameARCore *frame,
                                        int cameraTextureId,
                                        int displayRotation) {
    // Frame dropping: skip if previous frame still processing
    bool expected = false;
    if (!_isProcessing.compare_exchange_strong(expected, true)) {
        __android_log_print(ANDROID_LOG_WARN, FRAME_TAP_TAG, "Frame dropped (previous still processing)");
        return;
    }

    VRO_ENV env = VROPlatformGetJNIEnv();

    // Get strong ref to listener (check if still alive)
    VRO_OBJECT listenerRef = VRO_NEW_LOCAL_REF(_listener_j);
    if (!listenerRef) {
        __android_log_print(ANDROID_LOG_WARN, FRAME_TAP_TAG, "Listener garbage collected, skipping frame");
        _isProcessing = false;
        return;
    }

    // Extract camera from frame
    std::shared_ptr<VROARCameraARCore> cameraShared =
        std::dynamic_pointer_cast<VROARCameraARCore>(frame->getCamera());
    if (!cameraShared) {
        __android_log_print(ANDROID_LOG_ERROR, FRAME_TAP_TAG, "Failed to get ARCore camera");
        VRO_DELETE_LOCAL_REF(listenerRef);
        _isProcessing = false;
        return;
    }
    VROARCameraARCore *camera = cameraShared.get();

    // Create TextureInfo object
    jobject textureInfo = createTextureInfo(env, frame, camera, cameraTextureId, displayRotation);
    if (!textureInfo) {
        __android_log_print(ANDROID_LOG_ERROR, FRAME_TAP_TAG, "Failed to create TextureInfo");
        VRO_DELETE_LOCAL_REF(listenerRef);
        _isProcessing = false;
        return;
    }

    // Invoke onTextureFrame callback
    try {
        env->CallVoidMethod(listenerRef, _onTextureFrameMethod, textureInfo);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            __android_log_print(ANDROID_LOG_ERROR, FRAME_TAP_TAG, "Exception in onTextureFrame callback");
        }
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, FRAME_TAP_TAG, "Unexpected exception in onTextureFrame");
    }

    // CPU image path (optional)
    if (_enableCpuImages) {
        arcore::Image *cpuImageSource = nullptr;
        jobject cpuImage = createCpuImage(env, frame, camera, displayRotation, &cpuImageSource);
        if (cpuImage) {
            try {
                env->CallVoidMethod(listenerRef, _onCpuImageFrameMethod, cpuImage);
                if (env->ExceptionCheck()) {
                    env->ExceptionDescribe();
                    env->ExceptionClear();
                    __android_log_print(ANDROID_LOG_ERROR, FRAME_TAP_TAG, "Exception in onCpuImageFrame callback");
                }
            } catch (...) {
                __android_log_print(ANDROID_LOG_ERROR, FRAME_TAP_TAG, "Unexpected exception in onCpuImageFrame");
            }
            VRO_DELETE_LOCAL_REF(cpuImage);
        }
        // Release the ARCore image only now: the Java callback read the plane
        // buffers synchronously above, and they alias this image's memory.
        delete cpuImageSource;
    }

    VRO_DELETE_LOCAL_REF(textureInfo);
    VRO_DELETE_LOCAL_REF(listenerRef);
    _isProcessing = false;
}

jobject VROFrameTapListener::createTextureInfo(VRO_ENV env,
                                                VROARFrameARCore *frame,
                                                VROARCameraARCore *camera,
                                                int cameraTextureId,
                                                int displayRotation) {
    // REMOVED tracking state check - camera texture is valid immediately, even without full AR tracking
    // The AR scene can render and camera frames exist regardless of tracking state.
    // if (camera->getTrackingState() != VROARTrackingState::Normal) {
    //     __android_log_print(ANDROID_LOG_WARN, FRAME_TAP_TAG,
    //         "Skipping frame - camera not tracking (state: %d)", camera->getTrackingState());
    //     return nullptr;
    // }

    // Image dimensions come from camera metadata (ArCameraIntrinsics), NOT
    // from acquiring the CPU image. The previous loadImageData() here called
    // ArFrame_acquireCameraImage on EVERY camera frame (30Hz) just to read
    // getImageSize() — exhausting ARCore's small CPU-image pool (~50% acquire
    // failures), keeping the camera HAL's YUV stream hot, starving VIO into
    // tracking flaps, and cooking the device to thermal-critical during the
    // pre-scan wizard. Dims never change within a session, so fetch once.
    if (_cachedImageWidth <= 0 || _cachedImageHeight <= 0) {
        frame->getFrameInternal()->getImageDimensions(&_cachedImageWidth,
                                                      &_cachedImageHeight);
        if (_cachedImageWidth <= 0 || _cachedImageHeight <= 0) {
            __android_log_print(ANDROID_LOG_WARN, FRAME_TAP_TAG,
                "Skipping frame - camera image dimensions not yet available");
            return nullptr;
        }
        __android_log_print(ANDROID_LOG_INFO, FRAME_TAP_TAG,
            "Camera image dimensions (landscape): %dx%d",
            _cachedImageWidth, _cachedImageHeight);
    }

    // Timestamp (nanoseconds)
    double frameTimestamp = frame->getTimestamp();
    jlong timestampNs = (jlong)(frameTimestamp * 1e9);

    _frameCounter++;
    bool shouldLog = (_frameCounter == 1 || _frameCounter % 30 == 0);

    if (shouldLog) {
        __android_log_print(ANDROID_LOG_DEBUG, FRAME_TAP_TAG,
            "[Frame %d] Timestamp: %.6f seconds -> %lld ns",
            _frameCounter, frameTimestamp, (long long)timestampNs);
    }

    // Texture dimensions - full camera resolution rotated to display
    // orientation (portrait swaps the landscape metadata dims), matching what
    // getRotatedImageSize() used to return from the acquired CPU image.
    bool isPortrait = (displayRotation == 0 || displayRotation == 2);
    jint textureWidth = isPortrait ? (jint)_cachedImageHeight : (jint)_cachedImageWidth;
    jint textureHeight = isPortrait ? (jint)_cachedImageWidth : (jint)_cachedImageHeight;

    if (textureWidth <= 0 || textureHeight <= 0) {
        __android_log_print(ANDROID_LOG_ERROR, FRAME_TAP_TAG,
            "Invalid texture dimensions: %dx%d", textureWidth, textureHeight);
        return nullptr;
    }

    // Texture transform matrix
    VROVector3f BL, BR, TL, TR;
    frame->getBackgroundTexcoords(&BL, &BR, &TL, &TR);
    float texTransform[16];
    extractTextureTransform(BL, BR, TL, TR, texTransform);
    jfloatArray texTransformArray = env->NewFloatArray(16);
    env->SetFloatArrayRegion(texTransformArray, 0, 16, texTransform);

    // View matrix: Use camera rotation as view matrix (simplified)
    // For proper view matrix, would need to construct from rotation + position and invert
    VROMatrix4f rotationMatrix = camera->getRotation();
    jfloatArray viewMatrixArray = env->NewFloatArray(16);
    env->SetFloatArrayRegion(viewMatrixArray, 0, 16, rotationMatrix.getArray());

    // Projection matrix: Use identity for now (TODO: compute proper projection)
    VROMatrix4f projectionMatrix = VROMatrix4f::identity();
    jfloatArray projectionMatrixArray = env->NewFloatArray(16);
    env->SetFloatArrayRegion(projectionMatrixArray, 0, 16, projectionMatrix.getArray());

    // Camera intrinsics - get raw values from ARCore (in landscape coordinates).
    // Deliberately NOT camera->getImageIntrinsics(): that wrapper calls
    // loadImageData() (a per-frame CPU image acquire) and silently leaves these
    // uninitialized when tracking is lost. The direct metadata call needs no
    // image and always fills them.
    float fx = 0, fy = 0, cx = 0, cy = 0;
    frame->getFrameInternal()->getImageIntrinsics(&fx, &fy, &cx, &cy);

    // Rotate intrinsics to match the rotated image dimensions
    // ARCore always provides intrinsics for landscape orientation (e.g., 1920x1080)
    // When displaying in portrait, we need to rotate the principal point (cx, cy)
    // and swap the focal lengths (fx, fy) to match the rotated coordinate system.
    //
    // displayRotation values (Surface.ROTATION_X):
    //   0 = Portrait (90° CCW from landscape)
    //   1 = Landscape (no rotation)
    //   2 = Portrait upside-down (90° CW from landscape)
    //   3 = Reverse landscape (180° rotation)
    //
    // Raw (unrotated, landscape) image dimensions for the rotation
    // calculation — from the cached camera metadata, no image acquire.
    float rawW = (float)_cachedImageWidth;
    float rawH = (float)_cachedImageHeight;

    switch (displayRotation) {
        case 0: {
            // Portrait: 90° CCW rotation
            // Point (x,y) in WxH -> (y, W-x) in HxW
            float new_cx = cy;
            float new_cy = rawW - cx;
            // Focal lengths swap because X/Y axes swap
            float new_fx = fy;
            float new_fy = fx;
            cx = new_cx;
            cy = new_cy;
            fx = new_fx;
            fy = new_fy;
            break;
        }
        case 2: {
            // Portrait upside-down: 90° CW rotation (270° CCW)
            // Point (x,y) in WxH -> (H-y, x) in HxW
            float new_cx = rawH - cy;
            float new_cy = cx;
            float new_fx = fy;
            float new_fy = fx;
            cx = new_cx;
            cy = new_cy;
            fx = new_fx;
            fy = new_fy;
            break;
        }
        case 3: {
            // Reverse landscape: 180° rotation
            // Point (x,y) in WxH -> (W-x, H-y) in WxH
            cx = rawW - cx;
            cy = rawH - cy;
            // Focal lengths don't swap for 180° rotation
            break;
        }
        // case 1: Normal landscape - no transformation needed
    }

    if (shouldLog) {
        __android_log_print(ANDROID_LOG_DEBUG, FRAME_TAP_TAG,
            "[Frame %d] Intrinsics: fx=%.1f fy=%.1f cx=%.1f cy=%.1f (rotation=%d)",
            _frameCounter, fx, fy, cx, cy, displayRotation);
    }

    // Create TextureInfo object
    // Constructor signature: (long, int, int, int, float[], int, int, float, float[], float[], float, float, float, float, int)
    jobject textureInfo = env->NewObject(_textureInfoClass, _textureInfoConstructor,
        timestampNs,              // long timestampNs
        cameraTextureId,          // int oesTextureId
        textureWidth,             // int textureWidth
        textureHeight,            // int textureHeight
        texTransformArray,        // float[] texTransform
        0,                        // int ignored
        0,                        // int ignored
        0.0f,                     // float ignored
        viewMatrixArray,          // float[] viewMatrix
        projectionMatrixArray,    // float[] projectionMatrix
        fx,                       // float focalLengthX
        fy,                       // float focalLengthY
        cx,                       // float principalPointX
        cy,                       // float principalPointY
        displayRotation           // int displayRotation
    );

    // Clean up local refs
    env->DeleteLocalRef(texTransformArray);
    env->DeleteLocalRef(viewMatrixArray);
    env->DeleteLocalRef(projectionMatrixArray);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return nullptr;
    }

    if (shouldLog) {
        __android_log_print(ANDROID_LOG_DEBUG, FRAME_TAP_TAG,
            "Created TextureInfo: texId=%d, size=%dx%d, rotation=%d",
            cameraTextureId, textureWidth, textureHeight, displayRotation);
    }

    return textureInfo;
}

jobject VROFrameTapListener::createCpuImage(VRO_ENV env,
                                            VROARFrameARCore *frame,
                                            VROARCameraARCore *camera,
                                            int displayRotation,
                                            arcore::Image **outImage) {
    *outImage = nullptr;

    // Get ARCore camera image
    arcore::Image *image = nullptr;
    arcore::ImageRetrievalStatus status = frame->getFrameInternal()->acquireCameraImage(&image);

    if (status != arcore::ImageRetrievalStatus::Success || !image) {
        __android_log_print(ANDROID_LOG_WARN, FRAME_TAP_TAG,
            "Failed to acquire camera image: status=%d", (int)status);
        return nullptr;
    }

    // Extract image properties
    int32_t width = image->getWidth();
    int32_t height = image->getHeight();
    int32_t format = image->getFormat();

    // Verify YUV_420_888 format
    if (format != 35) { // AIMAGE_FORMAT_YUV_420_888
        __android_log_print(ANDROID_LOG_ERROR, FRAME_TAP_TAG,
            "Unexpected image format: %d (expected YUV_420_888)", format);
        delete image;
        return nullptr;
    }

    // Get plane data
    const uint8_t *yData, *uData, *vData;
    int yLength, uLength, vLength;
    image->getPlaneData(0, &yData, &yLength);
    image->getPlaneData(1, &uData, &uLength);
    image->getPlaneData(2, &vData, &vLength);

    int32_t yStride = image->getPlaneRowStride(0);
    int32_t uvStride = image->getPlaneRowStride(1);
    int32_t uvPixelStride = image->getPlanePixelStride(1);

    // NOTE (2026-08-22): a full per-pixel YUV→RGBA conversion used to live here.
    // It allocated width*height*4 bytes per frame, wrapped them in a direct
    // ByteBuffer... and then never passed that buffer to the CpuImage
    // constructor (see the signature above: 3 ByteBuffers only, y/u/v). Nothing
    // on the Java side ever read it — CpuImage has no rgba field, and
    // ViroWebRTCBridge does its own yuvToBitmap from the y/u/v planes. So every
    // frame paid a full-frame integer-math double loop AND leaked the buffer
    // (8.3 MB/frame at 1080p, 33 MB/frame at 4K). Deleted outright rather than
    // "fixed" with a free() — the correct amount of this work is zero. This is
    // what made the CPU-image tap unusable for a continuous frame roll.

    // Create Java ByteBuffers (direct buffers for native memory)
    jobject yBuffer = env->NewDirectByteBuffer((void*)yData, yLength);
    jobject uBuffer = env->NewDirectByteBuffer((void*)uData, uLength);
    jobject vBuffer = env->NewDirectByteBuffer((void*)vData, vLength);

    // Get view/projection matrices and intrinsics (same as TextureInfo)
    VROMatrix4f rotationMatrix = camera->getRotation();
    jfloatArray viewMatrixArray = env->NewFloatArray(16);
    env->SetFloatArrayRegion(viewMatrixArray, 0, 16, rotationMatrix.getArray());

    VROMatrix4f projectionMatrix = VROMatrix4f::identity();
    jfloatArray projectionMatrixArray = env->NewFloatArray(16);
    env->SetFloatArrayRegion(projectionMatrixArray, 0, 16, projectionMatrix.getArray());

    // Direct metadata call — same rationale as createTextureInfo (no image
    // dependency, always initialized).
    float fx = 0, fy = 0, cx = 0, cy = 0;
    frame->getFrameInternal()->getImageIntrinsics(&fx, &fy, &cx, &cy);

    // Rotate intrinsics to match display orientation (same logic as createTextureInfo)
    // Note: CpuImage uses raw ARCore dimensions (width, height), but for consistency
    // with how the data will be processed, we rotate the intrinsics to match the
    // display orientation.
    float rawW = (float)width;   // Raw ARCore image width (landscape)
    float rawH = (float)height;  // Raw ARCore image height (landscape)

    switch (displayRotation) {
        case 0: {
            // Portrait: 90° CCW rotation
            float new_cx = cy;
            float new_cy = rawW - cx;
            float new_fx = fy;
            float new_fy = fx;
            cx = new_cx;
            cy = new_cy;
            fx = new_fx;
            fy = new_fy;
            break;
        }
        case 2: {
            // Portrait upside-down: 90° CW rotation
            float new_cx = rawH - cy;
            float new_cy = cx;
            float new_fx = fy;
            float new_fy = fx;
            cx = new_cx;
            cy = new_cy;
            fx = new_fx;
            fy = new_fy;
            break;
        }
        case 3: {
            // Reverse landscape: 180° rotation
            cx = rawW - cx;
            cy = rawH - cy;
            break;
        }
        // case 1: Normal landscape - no transformation needed
    }

    jlong timestampNs = (jlong)(frame->getTimestamp() * 1e9);

    // Create CpuImage object
    jobject cpuImage = env->NewObject(_cpuImageClass, _cpuImageConstructor,
        timestampNs,            // long timestampNs
        yBuffer,                // ByteBuffer y
        uBuffer,                // ByteBuffer u
        vBuffer,                // ByteBuffer v
        yStride,                // int yStride
        uvStride,               // int uvStride
        uvPixelStride,          // int uvPixelStride
        width,                  // int width
        height,                 // int height
        viewMatrixArray,        // float[] viewMatrix
        projectionMatrixArray,  // float[] projectionMatrix
        fx, fy, cx, cy,         // float focal/principal
        displayRotation         // int displayRotation
    );

    // Clean up
    env->DeleteLocalRef(viewMatrixArray);
    env->DeleteLocalRef(projectionMatrixArray);
    env->DeleteLocalRef(yBuffer);
    env->DeleteLocalRef(uBuffer);
    env->DeleteLocalRef(vBuffer);

    // Hand ownership to the caller — the buffers above alias this image, so it
    // must outlive the Java callback (see the header note).
    *outImage = image;

    return cpuImage;
}

void VROFrameTapListener::extractTextureTransform(VROVector3f BL, VROVector3f BR,
                                                   VROVector3f TL, VROVector3f TR,
                                                   float outTransform[16]) {
    // ARCore provides texture coordinates for the 4 corners of the viewport.
    // We need to convert this to a 4x4 column-major transform matrix.
    //
    // The texture coordinates define how the camera texture is mapped to the viewport:
    // BL = bottom-left, BR = bottom-right, TL = top-left, TR = top-right
    //
    // For simplicity, we'll create an identity matrix and encode the scale/offset.
    // A more accurate approach would compute the affine transform from corner points.

    // For now, use a simple scale/offset based on BL and TR corners
    float scaleX = TR.x - BL.x;
    float scaleY = TR.y - BL.y;
    float offsetX = BL.x;
    float offsetY = BL.y;

    // Create column-major 4x4 matrix
    // [ scaleX    0       0   offsetX ]
    // [   0    scaleY    0   offsetY ]
    // [   0       0      1      0    ]
    // [   0       0      0      1    ]

    outTransform[0] = scaleX;
    outTransform[1] = 0.0f;
    outTransform[2] = 0.0f;
    outTransform[3] = 0.0f;

    outTransform[4] = 0.0f;
    outTransform[5] = scaleY;
    outTransform[6] = 0.0f;
    outTransform[7] = 0.0f;

    outTransform[8] = 0.0f;
    outTransform[9] = 0.0f;
    outTransform[10] = 1.0f;
    outTransform[11] = 0.0f;

    outTransform[12] = offsetX;
    outTransform[13] = offsetY;
    outTransform[14] = 0.0f;
    outTransform[15] = 1.0f;
}