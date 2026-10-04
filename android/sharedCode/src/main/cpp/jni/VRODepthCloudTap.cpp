//
//  VRODepthCloudTap.cpp
//  ViroRenderer
//
//  See VRODepthCloudTap.h.
//

#include "VRODepthCloudTap.h"
#include "arcore/ARCore_API.h"
#include "VRODefines.h"
#include VRO_C_INCLUDE
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#define DEPTH_TAP_TAG "ViroDepthTap"

// Sample cadence. ARCore depth updates at camera rate but neighbouring frames
// are nearly identical; 5Hz keeps the CPU-image acquire far from the pool
// exhaustion that a per-frame acquire caused (see VROFrameTapListener).
static const int64_t kTickIntervalNs = 200LL * 1000 * 1000;
// Target samples across the depth image width (~80x60 grid on 160x120).
static const int kTargetGridWidth = 80;
static const float kMinDepthM = 0.20f;
static const float kMaxDepthM = 4.0f;
// Reject "flying pixels" on depth edges: any 4-neighbour differing by more
// than this fraction of the centre depth.
static const float kEdgeRelTolerance = 0.06f;
static const float kVoxelM = 0.03f;
static const size_t kMaxVoxels = 300000;

VRODepthCloudTap &VRODepthCloudTap::instance() {
    static VRODepthCloudTap sInstance;
    return sInstance;
}

void VRODepthCloudTap::setEnabled(bool enabled) {
    if (enabled && !_enabled) {
        reset();
        std::lock_guard<std::mutex> lock(_mutex);
        _hasPose = false;
    }
    _enabled = enabled;
}

void VRODepthCloudTap::reset() {
    std::lock_guard<std::mutex> lock(_mutex);
    _voxels.clear();
    _pending.clear();
    _ticks = 0;
}

void VRODepthCloudTap::drain(std::vector<float> &out) {
    std::lock_guard<std::mutex> lock(_mutex);
    out.swap(_pending);
    _pending.clear();
}

bool VRODepthCloudTap::getPose(float out[16]) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_hasPose) return false;
    memcpy(out, _pose, sizeof(_pose));
    return true;
}

void VRODepthCloudTap::getStats(int32_t out[4]) {
    std::lock_guard<std::mutex> lock(_mutex);
    out[0] = (int32_t) _voxels.size();
    out[1] = _ticks;
    out[2] = _depthW;
    out[3] = _depthH;
}

static inline uint64_t voxelKey(float x, float y, float z) {
    // 21 bits per axis at 3cm = ±31m around the session origin.
    const int64_t off = 1 << 20;
    uint64_t ix = (uint64_t) ((int64_t) floorf(x / kVoxelM) + off) & 0x1FFFFF;
    uint64_t iy = (uint64_t) ((int64_t) floorf(y / kVoxelM) + off) & 0x1FFFFF;
    uint64_t iz = (uint64_t) ((int64_t) floorf(z / kVoxelM) + off) & 0x1FFFFF;
    return (ix << 42) | (iy << 21) | iz;
}

static inline int clamp255(float v) {
    return v < 0.f ? 0 : (v > 255.f ? 255 : (int) v);
}

void VRODepthCloudTap::process(arcore::Frame *frame) {
    if (!_enabled || frame == nullptr) return;
    if (frame->getTrackingState() != arcore::TrackingState::Tracking) return;

    // Pose every frame (cheap) so the PiP camera is as fresh as the AR view.
    float pose[16];
    frame->getCameraPoseMatrix(pose);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        memcpy(_pose, pose, sizeof(pose));
        _hasPose = true;
    }

    int64_t ts = frame->getTimestampNs();
    if (ts - _lastTickNs < kTickIntervalNs && ts >= _lastTickNs) return;
    _lastTickNs = ts;

    arcore::Image *depth = nullptr;
    if (frame->acquireDepthImage(&depth) != arcore::ImageRetrievalStatus::Success || !depth) {
        return;
    }
    arcore::Image *color = nullptr;
    if (frame->acquireCameraImage(&color) != arcore::ImageRetrievalStatus::Success || !color) {
        delete depth;
        return;
    }

    const int dw = depth->getWidth();
    const int dh = depth->getHeight();
    const int cw = color->getWidth();
    const int ch = color->getHeight();
    const uint8_t *dData = nullptr, *yData = nullptr, *uData = nullptr, *vData = nullptr;
    int dLen = 0, yLen = 0, uLen = 0, vLen = 0;
    depth->getPlaneData(0, &dData, &dLen);
    color->getPlaneData(0, &yData, &yLen);
    color->getPlaneData(1, &uData, &uLen);
    color->getPlaneData(2, &vData, &vLen);
    const int dRowStride = depth->getPlaneRowStride(0);
    const int yRowStride = color->getPlaneRowStride(0);
    const int uvRowStride = color->getPlaneRowStride(1);
    const int uvPixStride = color->getPlanePixelStride(1);

    // CPU-image intrinsics (landscape sensor frame, same frame as the depth).
    float fx = 0, fy = 0, cx = 0, cy = 0;
    frame->getImageIntrinsics(&fx, &fy, &cx, &cy);

    if (dw <= 0 || dh <= 0 || cw <= 0 || ch <= 0 || !dData || !yData || !uData || !vData
            || fx <= 0 || fy <= 0) {
        delete color;
        delete depth;
        return;
    }

    const float sx = (float) cw / (float) dw;
    const float sy = (float) ch / (float) dh;
    const int stride = std::max(1, dw / kTargetGridWidth);

    auto depthAt = [&](int u, int v) -> float {
        const uint16_t *row = (const uint16_t *) (dData + (size_t) v * dRowStride);
        return row[u] * 0.001f;
    };

    std::vector<float> fresh;
    fresh.reserve((size_t) (dw / stride) * (dh / stride) * 4);

    std::lock_guard<std::mutex> lock(_mutex);
    _depthW = dw;
    _depthH = dh;
    _ticks++;

    for (int v = 1; v < dh - 1; v += stride) {
        for (int u = 1; u < dw - 1; u += stride) {
            float d = depthAt(u, v);
            if (d < kMinDepthM || d > kMaxDepthM) continue;

            float tol = d * kEdgeRelTolerance;
            if (fabsf(depthAt(u - 1, v) - d) > tol || fabsf(depthAt(u + 1, v) - d) > tol ||
                fabsf(depthAt(u, v - 1) - d) > tol || fabsf(depthAt(u, v + 1) - d) > tol) {
                continue;
            }

            // Depth pixel centre → CPU-image pixel (pure scale; same FOV).
            float pu = (u + 0.5f) * sx - 0.5f;
            float pv = (v + 0.5f) * sy - 0.5f;
            int iu = std::min(cw - 1, std::max(0, (int) (pu + 0.5f)));
            int iv = std::min(ch - 1, std::max(0, (int) (pv + 0.5f)));

            // Camera frame. Image: +x right, +y down, +z forward (CV). ARCore
            // camera pose is GL: +x right, +y up, -z forward → flip y and z.
            float xc = (pu - cx) / fx * d;
            float yc = -(pv - cy) / fy * d;
            float zc = -d;

            float wx = pose[0] * xc + pose[4] * yc + pose[8] * zc + pose[12];
            float wy = pose[1] * xc + pose[5] * yc + pose[9] * zc + pose[13];
            float wz = pose[2] * xc + pose[6] * yc + pose[10] * zc + pose[14];

            if (_voxels.size() >= kMaxVoxels) break;
            if (!_voxels.insert(voxelKey(wx, wy, wz)).second) continue;

            float Y = yData[(size_t) iv * yRowStride + iu];
            size_t uvIdx = (size_t) (iv / 2) * uvRowStride + (size_t) (iu / 2) * uvPixStride;
            if ((int) uvIdx >= uLen || (int) uvIdx >= vLen) continue;
            float U = (float) uData[uvIdx] - 128.f;
            float V = (float) vData[uvIdx] - 128.f;
            int r = clamp255(Y + 1.402f * V);
            int g = clamp255(Y - 0.344136f * U - 0.714136f * V);
            int b = clamp255(Y + 1.772f * U);
            int32_t argb = (int32_t) (0xFF000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b);
            float argbBits;
            memcpy(&argbBits, &argb, sizeof(argbBits));

            fresh.push_back(wx);
            fresh.push_back(wy);
            fresh.push_back(wz);
            fresh.push_back(argbBits);
        }
    }

    _pending.insert(_pending.end(), fresh.begin(), fresh.end());

    if (_ticks == 1 || _ticks % 25 == 0) {
        __android_log_print(ANDROID_LOG_INFO, DEPTH_TAP_TAG,
            "tick %d: depth %dx%d cpu %dx%d stride %d +%zu pts, %zu voxels total",
            _ticks, dw, dh, cw, ch, stride, fresh.size() / 4, _voxels.size());
    }

    delete color;
    delete depth;
}

// ── JNI: com.viro.core.DepthCloudTap ──

extern "C" {

JNIEXPORT void JNICALL
Java_com_viro_core_DepthCloudTap_nativeSetEnabled(JNIEnv *env, jclass clazz, jboolean enabled) {
    VRODepthCloudTap::instance().setEnabled(enabled == JNI_TRUE);
    __android_log_print(ANDROID_LOG_INFO, DEPTH_TAP_TAG, "enabled=%d", (int) enabled);
}

JNIEXPORT void JNICALL
Java_com_viro_core_DepthCloudTap_nativeReset(JNIEnv *env, jclass clazz) {
    VRODepthCloudTap::instance().reset();
}

JNIEXPORT jfloatArray JNICALL
Java_com_viro_core_DepthCloudTap_nativeDrain(JNIEnv *env, jclass clazz) {
    std::vector<float> pts;
    VRODepthCloudTap::instance().drain(pts);
    jfloatArray arr = env->NewFloatArray((jsize) pts.size());
    if (arr && !pts.empty()) {
        env->SetFloatArrayRegion(arr, 0, (jsize) pts.size(), pts.data());
    }
    return arr;
}

JNIEXPORT jfloatArray JNICALL
Java_com_viro_core_DepthCloudTap_nativeGetPose(JNIEnv *env, jclass clazz) {
    float pose[16];
    if (!VRODepthCloudTap::instance().getPose(pose)) return nullptr;
    jfloatArray arr = env->NewFloatArray(16);
    if (arr) env->SetFloatArrayRegion(arr, 0, 16, pose);
    return arr;
}

JNIEXPORT jintArray JNICALL
Java_com_viro_core_DepthCloudTap_nativeGetStats(JNIEnv *env, jclass clazz) {
    int32_t stats[4];
    VRODepthCloudTap::instance().getStats(stats);
    jintArray arr = env->NewIntArray(4);
    if (arr) env->SetIntArrayRegion(arr, 0, 4, (const jint *) stats);
    return arr;
}

}
