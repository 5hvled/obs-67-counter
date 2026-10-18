#pragma once
#include "sixty-seven-filter.hpp"
#include <cstdint>

struct PoseArmLandmarks {
    HandPoint left_shoulder;
    HandPoint left_elbow;
    HandPoint left_wrist;
    HandPoint right_shoulder;
    HandPoint right_elbow;
    HandPoint right_wrist;

    bool left_forearm_valid() const
    {
        return left_elbow.valid && left_wrist.valid;
    }

    bool right_forearm_valid() const
    {
        return right_elbow.valid && right_wrist.valid;
    }

    bool both_forearms_valid() const
    {
        return left_forearm_valid() && right_forearm_valid();
    }
};

// Runs MoveNet SinglePose Lightning on a BGRA frame.
// Coordinates are returned normalized to the original source frame.
bool detect_pose_bgra(const uint8_t *data, uint32_t width, uint32_t height,
                      uint32_t stride, float min_confidence,
                      PoseArmLandmarks &pose);

// Explicitly tears down the lazily-loaded ONNX Runtime session.
void pose_detector_shutdown();
