//
//  VRODepthCloudTap.h
//  ViroRenderer
//
//  On-device colored point cloud from ARCore depth, for the live-scan PiP.
//
//  Every ~200ms (render thread, right after the FrameTap dispatch) this:
//    1. acquires ARCore's 16-bit depth image (mm) and the CPU YUV camera image
//       for the SAME frame,
//    2. unprojects a strided grid of depth pixels with the CPU-image intrinsics
//       (depth is FOV-aligned with the CPU image, so a depth pixel maps to the
//       CPU image by a pure scale — that is the whole color↔depth registration),
//    3. samples the color at that CPU-image pixel,
//    4. transforms the point to ARCore world with the physical-camera pose,
//    5. voxel-dedups (first observation wins) and queues new points.
//
//  Java drains the queue (com.viro.core.DepthCloudTap.nativeDrain) and reads
//  the latest camera pose (nativeGetPose) — both in ARCore world, which is the
//  same world Viro's camera transform reports, so no frame mapping is needed.
//
//  Process-global singleton: there is only ever one AR session.
//

#ifndef VRODepthCloudTap_h
#define VRODepthCloudTap_h

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace arcore {
    class Frame;
}

class VRODepthCloudTap {
public:
    static VRODepthCloudTap &instance();

    // Render thread. Cheap no-op unless enabled.
    void process(arcore::Frame *frame);

    // Off->on = a new scan-screen visit = a new ARCore session (new world
    // origin): drops the previous session's points and pose.
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    // Clears the voxel set and any undrained points (new scan).
    void reset();

    // Moves queued points out: interleaved [x, y, z, argbBits] (argb stored as
    // the float bit pattern of an int32).
    void drain(std::vector<float> &out);

    // Latest physical-camera pose (column-major world-from-camera). Returns
    // false if no tracked frame has been seen yet.
    bool getPose(float out[16]);

    // [totalVoxels, ticks, depthW, depthH]
    void getStats(int32_t out[4]);

private:
    VRODepthCloudTap() = default;

    std::atomic<bool> _enabled{false};
    std::atomic<bool> _resetRequested{false};
    int64_t _lastTickNs = 0;

    std::mutex _mutex;                     // guards everything below
    std::unordered_set<uint64_t> _voxels;
    std::vector<float> _pending;
    float _pose[16] = {0};
    bool _hasPose = false;
    int32_t _ticks = 0;
    int32_t _depthW = 0;
    int32_t _depthH = 0;
};

#endif /* VRODepthCloudTap_h */
