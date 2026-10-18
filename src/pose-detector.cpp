#include "pose-detector.hpp"

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT

#include <obs-module.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr int kFallbackInputWidth = 192;
constexpr int kFallbackInputHeight = 192;

constexpr size_t kLeftShoulder = 5;
constexpr size_t kRightShoulder = 6;
constexpr size_t kLeftElbow = 7;
constexpr size_t kRightElbow = 8;
constexpr size_t kLeftWrist = 9;
constexpr size_t kRightWrist = 10;

using OrtGetApiBaseFn = const OrtApiBase *(ORT_API_CALL *)();

class PoseRuntime {
public:
    ~PoseRuntime()
    {
        shutdown();
    }

    bool ensure_loaded()
    {
        if (session_)
            return true;
        if (load_attempted_)
            return false;

        load_attempted_ = true;

        char *runtime_path = obs_module_file("onnxruntime.dll");
        if (!runtime_path) {
            blog(LOG_ERROR,
                 "[obs-67-counter] MoveNet: could not resolve onnxruntime.dll");
            return false;
        }

        ort_library_ = os_dlopen(runtime_path);
        if (!ort_library_) {
            blog(LOG_ERROR,
                 "[obs-67-counter] MoveNet: failed to load %s",
                 runtime_path);
            bfree(runtime_path);
            return false;
        }
        bfree(runtime_path);

        auto get_api_base = reinterpret_cast<OrtGetApiBaseFn>(
            os_dlsym(ort_library_, "OrtGetApiBase"));
        if (!get_api_base) {
            blog(LOG_ERROR,
                 "[obs-67-counter] MoveNet: OrtGetApiBase export not found");
            cleanup(false);
            return false;
        }

        const OrtApiBase *api_base = get_api_base();
        const OrtApi *api = api_base ? api_base->GetApi(ORT_API_VERSION) : nullptr;
        if (!api) {
            blog(LOG_ERROR,
                 "[obs-67-counter] MoveNet: ONNX Runtime API v%d unavailable",
                 ORT_API_VERSION);
            cleanup(false);
            return false;
        }

        Ort::InitApi(api);

        char *model_path = obs_module_file("movenet_singlepose_lightning.onnx");
        if (!model_path) {
            blog(LOG_ERROR,
                 "[obs-67-counter] MoveNet: model file was not found");
            cleanup(false);
            return false;
        }

        try {
            env_ = std::make_unique<Ort::Env>(
                ORT_LOGGING_LEVEL_WARNING, "obs-67-counter");

            Ort::SessionOptions options;
            options.SetGraphOptimizationLevel(
                GraphOptimizationLevel::ORT_ENABLE_ALL);
            options.SetIntraOpNumThreads(2);
            options.SetInterOpNumThreads(1);

#ifdef _WIN32
            wchar_t *wide_model_path = nullptr;
            const size_t converted = os_utf8_to_wcs_ptr(
                model_path, 0, &wide_model_path);
            if (!converted || !wide_model_path) {
                blog(LOG_ERROR,
                     "[obs-67-counter] MoveNet: model path conversion failed");
                bfree(model_path);
                model_path = nullptr;
                cleanup(false);
                return false;
            }

            session_ = std::make_unique<Ort::Session>(
                *env_, wide_model_path, options);
            bfree(wide_model_path);
#else
            session_ = std::make_unique<Ort::Session>(
                *env_, model_path, options);
#endif
            bfree(model_path);
            model_path = nullptr;

            Ort::AllocatorWithDefaultOptions allocator;
            auto input_name =
                session_->GetInputNameAllocated(0, allocator);
            auto output_name =
                session_->GetOutputNameAllocated(0, allocator);
            input_name_ = input_name.get();
            output_name_ = output_name.get();

            auto input_info =
                session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
            input_type_ = input_info.GetElementType();
            const auto input_shape = input_info.GetShape();

            if (input_shape.size() != 4 ||
                input_shape[3] != 3) {
                blog(LOG_ERROR,
                     "[obs-67-counter] MoveNet: unexpected input shape");
                cleanup(false);
                return false;
            }

            input_height_ =
                input_shape[1] > 0 ? (int)input_shape[1] :
                                     kFallbackInputHeight;
            input_width_ =
                input_shape[2] > 0 ? (int)input_shape[2] :
                                     kFallbackInputWidth;

            if (input_width_ <= 0 || input_height_ <= 0) {
                blog(LOG_ERROR,
                     "[obs-67-counter] MoveNet: invalid model dimensions");
                cleanup(false);
                return false;
            }

            const size_t pixel_count =
                (size_t)input_width_ * input_height_ * 3;
            input_i32_.resize(pixel_count);
            input_u8_.resize(pixel_count);
            input_f32_.resize(pixel_count);

            blog(LOG_INFO,
                 "[obs-67-counter] MoveNet loaded: %dx%d input, type=%d",
                 input_width_, input_height_, (int)input_type_);
            return true;

        } catch (const Ort::Exception &e) {
            blog(LOG_ERROR,
                 "[obs-67-counter] MoveNet initialization failed: %s",
                 e.what());
            if (model_path)
                bfree(model_path);
            cleanup(false);
            return false;
        } catch (...) {
            blog(LOG_ERROR,
                 "[obs-67-counter] MoveNet initialization failed");
            if (model_path)
                bfree(model_path);
            cleanup(false);
            return false;
        }
    }

    bool run(const uint8_t *bgra, uint32_t width, uint32_t height,
             uint32_t stride, float min_confidence,
             PoseArmLandmarks &pose)
    {
        pose = {};

        if (!bgra || width < 16 || height < 16 || stride < width * 4)
            return false;
        if (!ensure_loaded())
            return false;

        int resized_w = 0;
        int resized_h = 0;
        int pad_x = 0;
        int pad_y = 0;

        preprocess(bgra, width, height, stride,
                   resized_w, resized_h, pad_x, pad_y);

        try {
            Ort::MemoryInfo memory_info =
                Ort::MemoryInfo::CreateCpu(
                    OrtArenaAllocator, OrtMemTypeDefault);

            const std::array<int64_t, 4> shape = {
                1, input_height_, input_width_, 3};

            Ort::Value input{nullptr};
            if (input_type_ ==
                ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
                input = Ort::Value::CreateTensor<int32_t>(
                    memory_info,
                    input_i32_.data(), input_i32_.size(),
                    shape.data(), shape.size());
            } else if (input_type_ ==
                       ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) {
                input = Ort::Value::CreateTensor<uint8_t>(
                    memory_info,
                    input_u8_.data(), input_u8_.size(),
                    shape.data(), shape.size());
            } else if (input_type_ ==
                       ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                input = Ort::Value::CreateTensor<float>(
                    memory_info,
                    input_f32_.data(), input_f32_.size(),
                    shape.data(), shape.size());
            } else {
                if (!unsupported_type_logged_) {
                    blog(LOG_ERROR,
                         "[obs-67-counter] MoveNet: unsupported input type=%d",
                         (int)input_type_);
                    unsupported_type_logged_ = true;
                }
                return false;
            }

            const char *input_names[] = {input_name_.c_str()};
            const char *output_names[] = {output_name_.c_str()};

            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names, &input, 1,
                output_names, 1);

            if (outputs.empty() || !outputs[0].IsTensor())
                return false;

            const auto out_info =
                outputs[0].GetTensorTypeAndShapeInfo();
            if (out_info.GetElementType() !=
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                return false;
            if (out_info.GetElementCount() < 17 * 3)
                return false;

            const float *values =
                outputs[0].GetTensorData<float>();

            pose.left_shoulder =
                decode_keypoint(values, kLeftShoulder,
                                resized_w, resized_h,
                                pad_x, pad_y,
                                min_confidence);
            pose.right_shoulder =
                decode_keypoint(values, kRightShoulder,
                                resized_w, resized_h,
                                pad_x, pad_y,
                                min_confidence);
            pose.left_elbow =
                decode_keypoint(values, kLeftElbow,
                                resized_w, resized_h,
                                pad_x, pad_y,
                                min_confidence);
            pose.right_elbow =
                decode_keypoint(values, kRightElbow,
                                resized_w, resized_h,
                                pad_x, pad_y,
                                min_confidence);
            pose.left_wrist =
                decode_keypoint(values, kLeftWrist,
                                resized_w, resized_h,
                                pad_x, pad_y,
                                min_confidence);
            pose.right_wrist =
                decode_keypoint(values, kRightWrist,
                                resized_w, resized_h,
                                pad_x, pad_y,
                                min_confidence);

            return pose.both_forearms_valid();

        } catch (const Ort::Exception &e) {
            const uint64_t now = os_gettime_ns();
            if (now - last_run_error_ns_ > 5000000000ULL) {
                blog(LOG_ERROR,
                     "[obs-67-counter] MoveNet inference failed: %s",
                     e.what());
                last_run_error_ns_ = now;
            }
            return false;
        }
    }

    void shutdown()
    {
        cleanup(true);
    }

private:
    void cleanup(bool allow_retry)
    {
        session_.reset();
        env_.reset();

        if (ort_library_) {
            os_dlclose(ort_library_);
            ort_library_ = nullptr;
        }

        input_name_.clear();
        output_name_.clear();
        input_i32_.clear();
        input_u8_.clear();
        input_f32_.clear();
        unsupported_type_logged_ = false;
        load_attempted_ = !allow_retry;
    }

    void preprocess(const uint8_t *bgra, uint32_t width,
                    uint32_t height, uint32_t stride,
                    int &resized_w, int &resized_h,
                    int &pad_x, int &pad_y)
    {
        const float scale = std::min(
            (float)input_width_ / (float)width,
            (float)input_height_ / (float)height);

        resized_w = std::max(
            1, std::min(input_width_,
                        (int)std::lround(width * scale)));
        resized_h = std::max(
            1, std::min(input_height_,
                        (int)std::lround(height * scale)));
        pad_x = (input_width_ - resized_w) / 2;
        pad_y = (input_height_ - resized_h) / 2;

        std::fill(input_i32_.begin(), input_i32_.end(), 0);
        std::fill(input_u8_.begin(), input_u8_.end(), 0);
        std::fill(input_f32_.begin(), input_f32_.end(), 0.0f);

        for (int dy = 0; dy < resized_h; ++dy) {
            const uint32_t sy = std::min<uint32_t>(
                height - 1,
                (uint32_t)(((uint64_t)(2 * dy + 1) * height) /
                           (uint64_t)(2 * resized_h)));

            for (int dx = 0; dx < resized_w; ++dx) {
                const uint32_t sx = std::min<uint32_t>(
                    width - 1,
                    (uint32_t)(((uint64_t)(2 * dx + 1) * width) /
                               (uint64_t)(2 * resized_w)));

                const uint8_t *pixel =
                    bgra + (size_t)sy * stride + (size_t)sx * 4;
                const uint8_t r = pixel[2];
                const uint8_t g = pixel[1];
                const uint8_t b = pixel[0];

                const size_t out =
                    ((size_t)(dy + pad_y) * input_width_ +
                     (size_t)(dx + pad_x)) * 3;

                input_i32_[out + 0] = (int32_t)r;
                input_i32_[out + 1] = (int32_t)g;
                input_i32_[out + 2] = (int32_t)b;

                input_u8_[out + 0] = r;
                input_u8_[out + 1] = g;
                input_u8_[out + 2] = b;

                input_f32_[out + 0] = (float)r;
                input_f32_[out + 1] = (float)g;
                input_f32_[out + 2] = (float)b;
            }
        }
    }

    HandPoint decode_keypoint(const float *values, size_t index,
                              int resized_w, int resized_h,
                              int pad_x, int pad_y,
                              float min_confidence) const
    {
        HandPoint p;

        const float model_y = values[index * 3 + 0];
        const float model_x = values[index * 3 + 1];
        const float confidence = values[index * 3 + 2];

        p.confidence = confidence;
        p.area = confidence;

        if (!std::isfinite(model_x) || !std::isfinite(model_y) ||
            !std::isfinite(confidence) ||
            confidence < min_confidence ||
            resized_w <= 0 || resized_h <= 0)
            return p;

        const float x_px = model_x * (float)input_width_;
        const float y_px = model_y * (float)input_height_;

        p.x = (x_px - (float)pad_x) / (float)resized_w;
        p.y = (y_px - (float)pad_y) / (float)resized_h;

        // Allow a tiny amount of model overshoot but reject points that
        // clearly land in the letterbox bars.
        if (p.x < -0.05f || p.x > 1.05f ||
            p.y < -0.05f || p.y > 1.05f)
            return p;

        p.x = std::clamp(p.x, 0.0f, 1.0f);
        p.y = std::clamp(p.y, 0.0f, 1.0f);
        p.valid = true;
        return p;
    }

    void *ort_library_ = nullptr;
    bool load_attempted_ = false;
    bool unsupported_type_logged_ = false;

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;

    std::string input_name_;
    std::string output_name_;
    ONNXTensorElementDataType input_type_ =
        ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    int input_width_ = kFallbackInputWidth;
    int input_height_ = kFallbackInputHeight;

    std::vector<int32_t> input_i32_;
    std::vector<uint8_t> input_u8_;
    std::vector<float> input_f32_;

    uint64_t last_run_error_ns_ = 0;
};

std::mutex g_pose_mutex;
std::unique_ptr<PoseRuntime> g_pose_runtime;

} // namespace

bool detect_pose_bgra(const uint8_t *data, uint32_t width, uint32_t height,
                      uint32_t stride, float min_confidence,
                      PoseArmLandmarks &pose)
{
    std::lock_guard<std::mutex> lock(g_pose_mutex);

    if (!g_pose_runtime)
        g_pose_runtime = std::make_unique<PoseRuntime>();

    return g_pose_runtime->run(
        data, width, height, stride, min_confidence, pose);
}

void pose_detector_shutdown()
{
    std::lock_guard<std::mutex> lock(g_pose_mutex);
    g_pose_runtime.reset();
}
