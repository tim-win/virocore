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
    _isProcessing(false) {

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
        jobject cpuImage = createCpuImage(env, frame, camera, displayRotation);
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
    // Check if camera is tracking - ARCore must be initialized before accessing image data
    if (camera->getTrackingState() != VROARTrackingState::Normal) {
        __android_log_print(ANDROID_LOG_WARN, FRAME_TAP_TAG,
            "Skipping frame - camera not tracking (state: %d)", camera->getTrackingState());
        return nullptr;
    }

    // Acquire ARCore camera image data (required for getImageSize())
    if (!camera->loadImageData()) {
        __android_log_print(ANDROID_LOG_WARN, FRAME_TAP_TAG,
            "Skipping frame - failed to acquire camera image data");
        return nullptr;
    }

    // Timestamp (nanoseconds)
    jlong timestampNs = (jlong)(frame->getTimestamp() * 1e9);

    // Texture dimensions - use FULL camera resolution, not cropped viewport size
    VROVector3f imageSize = camera->getRotatedImageSize();
    jint textureWidth = (jint)imageSize.x;
    jint textureHeight = (jint)imageSize.y;

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

    // Camera intrinsics
    float fx, fy, cx, cy;
    camera->getImageIntrinsics(&fx, &fy, &cx, &cy);

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

    __android_log_print(ANDROID_LOG_DEBUG, FRAME_TAP_TAG,
        "Created TextureInfo: texId=%d, size=%dx%d, rotation=%d",
        cameraTextureId, textureWidth, textureHeight, displayRotation);

    return textureInfo;
}

jobject VROFrameTapListener::createCpuImage(VRO_ENV env,
                                            VROARFrameARCore *frame,
                                            VROARCameraARCore *camera,
                                            int displayRotation) {
    // CPU image support requires ARCore Frame.acquireCameraImage()
    // This is not currently implemented in VROARCameraARCore, so we return nullptr
    // TODO: Implement CPU image extraction via ARCore C API
    __android_log_print(ANDROID_LOG_WARN, FRAME_TAP_TAG,
        "CPU image path not yet implemented, returning nullptr");
    return nullptr;
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