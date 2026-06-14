#include "RknnDetector.h"

#include "TimeUtils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

#if defined(AICAM_HAS_RKNN) && AICAM_HAS_RKNN
#include <rknn_api.h>
#endif

#if defined(AICAM_HAS_RGA) && AICAM_HAS_RGA
#include <rga/im2d.h>
#endif

namespace {

struct LetterboxInfo {
    float scale = 1.0f;
    int pad_x = 0;
    int pad_y = 0;
    int resized_width = 0;
    int resized_height = 0;
};

// 把 int 限制到 0~255，然后转成 uint8_t
inline uint8_t clampToByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

// 去掉字符串首尾空白字符
std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// 读取标签文件
std::vector<std::string> loadLabels(const std::string& path) {
    std::vector<std::string> labels;
    if (path.empty()) {
        return labels;
    }

    std::ifstream input(path.c_str());
    if (!input.is_open()) {
        std::cerr << "标签文件打开失败: " << path << std::endl;
        return labels;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.empty()) {
            labels.push_back(line);
        }
    }

    return labels;
}

void fillLetterboxedRgb(
    const CapturedFrame& frame,
    int dst_width,
    int dst_height,
    std::vector<uint8_t>& rgb,
    LetterboxInfo& info
) {
    rgb.assign(static_cast<size_t>(dst_width) * dst_height * 3, 114);

    const float scale_x = static_cast<float>(dst_width) / static_cast<float>(frame.width);
    const float scale_y = static_cast<float>(dst_height) / static_cast<float>(frame.height);
    info.scale = std::min(scale_x, scale_y);
    info.resized_width = std::max(1, static_cast<int>(std::round(frame.width * info.scale)));
    info.resized_height = std::max(1, static_cast<int>(std::round(frame.height * info.scale)));
    info.pad_x = (dst_width - info.resized_width) / 2;
    info.pad_y = (dst_height - info.resized_height) / 2;

    const size_t y_plane_size = static_cast<size_t>(frame.width) * frame.height;

    for (int y = 0; y < info.resized_height; ++y) {
        const int src_y = std::min(
            frame.height - 1,
            static_cast<int>(y / info.scale)
        );

        for (int x = 0; x < info.resized_width; ++x) {
            const int src_x = std::min(
                frame.width - 1,
                static_cast<int>(x / info.scale)
            );

            const size_t y_index = static_cast<size_t>(src_y) * frame.width + src_x;
            const size_t uv_index =
                y_plane_size +
                static_cast<size_t>(src_y / 2) * frame.width +
                static_cast<size_t>(src_x & ~1);

            const int y_value = static_cast<int>(frame.nv12[y_index]);
            const int u_value = static_cast<int>(frame.nv12[uv_index]) - 128;
            const int v_value = static_cast<int>(frame.nv12[uv_index + 1]) - 128;

            const int c = std::max(0, y_value - 16);
            const int d = u_value;
            const int e = v_value;

            const uint8_t r = clampToByte((298 * c + 409 * e + 128) >> 8);
            const uint8_t g = clampToByte((298 * c - 100 * d - 208 * e + 128) >> 8);
            const uint8_t b = clampToByte((298 * c + 516 * d + 128) >> 8);

            const int dst_x = info.pad_x + x;
            const int dst_y = info.pad_y + y;
            const size_t dst_index =
                (static_cast<size_t>(dst_y) * dst_width + static_cast<size_t>(dst_x)) * 3;

            rgb[dst_index + 0] = r;
            rgb[dst_index + 1] = g;
            rgb[dst_index + 2] = b;
        }
    }
}

#if defined(AICAM_HAS_RGA) && AICAM_HAS_RGA
struct RgaHandleGuard {
    rga_buffer_handle_t handle = 0;

    ~RgaHandleGuard() {
        if (handle != 0) {
            releasebuffer_handle(handle);
        }
    }
};

bool fillLetterboxedRgbWithRga(
    const CapturedFrame& frame,
    int dst_width,
    int dst_height,
    std::vector<uint8_t>& rgb,
    LetterboxInfo& info,
    std::string& error
) {
    if (frame.width <= 0 || frame.height <= 0 || dst_width <= 0 || dst_height <= 0) {
        error = "invalid frame or model input size";
        return false;
    }

    const size_t expected_nv12_size = static_cast<size_t>(frame.width) * frame.height * 3 / 2;
    if (frame.nv12.size() < expected_nv12_size) {
        error = "NV12 buffer is smaller than expected";
        return false;
    }

    const float scale_x = static_cast<float>(dst_width) / static_cast<float>(frame.width);
    const float scale_y = static_cast<float>(dst_height) / static_cast<float>(frame.height);
    info.scale = std::min(scale_x, scale_y);
    info.resized_width = std::max(1, static_cast<int>(std::round(frame.width * info.scale)));
    info.resized_height = std::max(1, static_cast<int>(std::round(frame.height * info.scale)));
    info.pad_x = (dst_width - info.resized_width) / 2;
    info.pad_y = (dst_height - info.resized_height) / 2;

    rgb.assign(static_cast<size_t>(dst_width) * dst_height * 3, 114);

    RgaHandleGuard src_handle;
    RgaHandleGuard dst_handle;
    src_handle.handle = importbuffer_virtualaddr(
        const_cast<uint8_t*>(frame.nv12.data()),
        static_cast<int>(expected_nv12_size)
    );
    dst_handle.handle = importbuffer_virtualaddr(
        rgb.data(),
        static_cast<int>(rgb.size())
    );

    if (src_handle.handle == 0 || dst_handle.handle == 0) {
        error = "importbuffer_virtualaddr failed";
        return false;
    }

    rga_buffer_t src = wrapbuffer_handle(
        src_handle.handle,
        frame.width,
        frame.height,
        RK_FORMAT_YCbCr_420_SP
    );
    rga_buffer_t dst = wrapbuffer_handle(
        dst_handle.handle,
        dst_width,
        dst_height,
        RK_FORMAT_RGB_888
    );

    im_rect src_rect {};
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = frame.width;
    src_rect.height = frame.height;

    im_rect dst_rect {};
    dst_rect.x = info.pad_x;
    dst_rect.y = info.pad_y;
    dst_rect.width = info.resized_width;
    dst_rect.height = info.resized_height;

    IM_STATUS status = imcheck(src, dst, src_rect, dst_rect);
    if (status != IM_STATUS_NOERROR) {
        error = std::string("imcheck failed: ") + imStrError(status);
        return false;
    }

    status = improcess(src, dst, {}, src_rect, dst_rect, {}, IM_SYNC);
    if (status != IM_STATUS_SUCCESS) {
        error = std::string("improcess failed: ") + imStrError(status);
        return false;
    }

    return true;
}
#endif

// 计算两个框重叠面积
float intersectionOverUnion(const ai::Detection& lhs, const ai::Detection& rhs) {
    const float inter_x1 = std::max(lhs.x1, rhs.x1);
    const float inter_y1 = std::max(lhs.y1, rhs.y1);
    const float inter_x2 = std::min(lhs.x2, rhs.x2);
    const float inter_y2 = std::min(lhs.y2, rhs.y2);

    const float inter_w = std::max(0.0f, inter_x2 - inter_x1);
    const float inter_h = std::max(0.0f, inter_y2 - inter_y1);
    const float inter_area = inter_w * inter_h;

    const float lhs_area = std::max(0.0f, lhs.x2 - lhs.x1) * std::max(0.0f, lhs.y2 - lhs.y1);
    const float rhs_area = std::max(0.0f, rhs.x2 - rhs.x1) * std::max(0.0f, rhs.y2 - rhs.y1);
    const float union_area = lhs_area + rhs_area - inter_area;

    if (union_area <= 0.0f) {
        return 0.0f;
    }

    return inter_area / union_area;
}

std::vector<ai::Detection> applyNms(
    std::vector<ai::Detection> detections,
    float nms_threshold,
    int max_results
) {
    std::sort(
        detections.begin(),
        detections.end(),
        [](const ai::Detection& lhs, const ai::Detection& rhs) {
            return lhs.score > rhs.score;
        }
    );

    std::vector<ai::Detection> kept;
    for (const auto& candidate : detections) {
        bool suppressed = false;
        for (const auto& selected : kept) {
            if (candidate.class_id == selected.class_id &&
                intersectionOverUnion(candidate, selected) > nms_threshold) {
                suppressed = true;
                break;
            }
        }

        if (!suppressed) {
            kept.push_back(candidate);
            if (max_results > 0 && static_cast<int>(kept.size()) >= max_results) {
                break;
            }
        }
    }

    return kept;
}

bool looksNormalized(float a, float b, float c, float d) {
    const float max_value = std::max(
        std::max(std::fabs(a), std::fabs(b)),
        std::max(std::fabs(c), std::fabs(d))
    );
    return max_value <= 2.0f;
}

bool inferTensorLayout(
    const std::vector<uint32_t>& dims,
    int& rows,
    int& attrs,
    bool& attr_major
) {
    if (dims.size() == 2) {
        if (dims[1] >= 6) {
            rows = static_cast<int>(dims[0]);
            attrs = static_cast<int>(dims[1]);
            attr_major = false;
            return true;
        }
        if (dims[0] >= 6) {
            rows = static_cast<int>(dims[1]);
            attrs = static_cast<int>(dims[0]);
            attr_major = true;
            return true;
        }
        return false;
    }

    if (dims.size() == 3) {
        const uint32_t second_last = dims[dims.size() - 2];
        const uint32_t last = dims[dims.size() - 1];

        if (last >= 6) {
            rows = static_cast<int>(second_last);
            attrs = static_cast<int>(last);
            attr_major = false;
            return true;
        }

        if (second_last >= 6) {
            rows = static_cast<int>(last);
            attrs = static_cast<int>(second_last);
            attr_major = true;
            return true;
        }
    }

    return false;
}

float readTensorValue(
    const float* data,
    int rows,
    int attrs,
    bool attr_major,
    int row,
    int attr
) {
    if (attr_major) {
        return data[attr * rows + row];
    }
    return data[row * attrs + attr];
}

float clampFloat(float value, float lower, float upper) {
    return std::max(lower, std::min(value, upper));
}

}  // namespace

// RknnDetector 的内部实现数据结构
struct RknnDetector::Impl {
#if defined(AICAM_HAS_RKNN) && AICAM_HAS_RKNN
    rknn_context context = 0;
    rknn_input_output_num io_num {};
    rknn_tensor_attr input_attr {};
    std::vector<rknn_tensor_attr> output_attrs;
#endif
    int input_width = 0;
    int input_height = 0;
    int input_channels = 0;
    std::vector<uint8_t> rgb_input;
    std::vector<std::string> labels;
    bool rga_fallback_reported = false;
};

RknnDetector::RknnDetector(AiConfig config)
    : config_(std::move(config)),
      impl_(new Impl()),
      initialized_(false) {}

RknnDetector::~RknnDetector() {
#if defined(AICAM_HAS_RKNN) && AICAM_HAS_RKNN
    if (impl_ != nullptr && impl_->context != 0) {
        rknn_destroy(impl_->context);
        impl_->context = 0;
    }
#endif
}

bool RknnDetector::initialize() {
    if (!config_.enabled) {
        disabled_reason_ = "AI disabled by configuration";
        return false;
    }

    if (config_.model_path.empty()) {
        disabled_reason_ = "AICAM_RKNN_MODEL is empty";
        return false;
    }
    // 加载标签文件
    impl_->labels = loadLabels(config_.labels_path);

// 检测rknn相关推理代码是否被编译
#if defined(AICAM_HAS_RKNN) && AICAM_HAS_RKNN
    // 初始化RKNN runtime运行时环境，
    // 加载模型，查询输入输出属性等准备工作
    const int ret = rknn_init(
        // 运行时句柄,
        // 保存模型加载后的状态和硬件资源
        &impl_->context,
        const_cast<char*>(config_.model_path.c_str()),
        0,
        RKNN_FLAG_PRIOR_MEDIUM,
        nullptr
    );
    // 把 RKNN 初始化失败的错误信息
    // 保存到对象内部的 disabled_reason_，
    // 方便后续打印或日志记录
    if (ret != RKNN_SUCC) {
        std::ostringstream oss;
        oss << "rknn_init failed, ret=" << ret;
        disabled_reason_ = oss.str();
        return false;
    }
    // 查询rknn输入输出数量，写入io_num
    // sizeof防止越界
    if (rknn_query(
            impl_->context,
            RKNN_QUERY_IN_OUT_NUM,
            &impl_->io_num,
            sizeof(impl_->io_num)) != RKNN_SUCC) {
        disabled_reason_ = "RKNN_QUERY_IN_OUT_NUM failed";
        return false;
    }

    std::memset(&impl_->input_attr, 0, sizeof(impl_->input_attr));
    // 指定并查询第0个输入，
    // 大部分模型只有一个输入
    impl_->input_attr.index = 0;
    if (rknn_query(
            impl_->context,
            RKNN_QUERY_INPUT_ATTR,
            &impl_->input_attr,
            sizeof(impl_->input_attr)) != RKNN_SUCC) {
        disabled_reason_ = "RKNN_QUERY_INPUT_ATTR failed";
        return false;
    }
    // 给输出tensor属性分配空间，并查询每个输出的属性
    impl_->output_attrs.resize(impl_->io_num.n_output);
    for (uint32_t i = 0; i < impl_->io_num.n_output; ++i) {
        std::memset(&impl_->output_attrs[i], 0, sizeof(rknn_tensor_attr));
        impl_->output_attrs[i].index = i;
        if (rknn_query(
                impl_->context,
                RKNN_QUERY_OUTPUT_ATTR,
                &impl_->output_attrs[i],
                sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
            std::ostringstream oss;
            oss << "RKNN_QUERY_OUTPUT_ATTR failed for output " << i;
            disabled_reason_ = oss.str();
            return false;
        }
    }

    if (impl_->input_attr.n_dims < 4) {
        disabled_reason_ = "unsupported RKNN input dims";
        return false;
    }
    // 检查输入格式并配置
    if (impl_->input_attr.fmt == RKNN_TENSOR_NCHW) {
        impl_->input_channels = static_cast<int>(impl_->input_attr.dims[1]);
        impl_->input_height = static_cast<int>(impl_->input_attr.dims[2]);
        impl_->input_width = static_cast<int>(impl_->input_attr.dims[3]);
    } else {
        impl_->input_height = static_cast<int>(impl_->input_attr.dims[1]);
        impl_->input_width = static_cast<int>(impl_->input_attr.dims[2]);
        impl_->input_channels = static_cast<int>(impl_->input_attr.dims[3]);
    }

    if (impl_->input_channels != 3) {
        disabled_reason_ = "current preprocessing only supports 3-channel models";
        return false;
    }

    std::cout << "RKNN detector ready"
              << " | model=" << config_.model_path
              << " | input=" << impl_->input_width << "x" << impl_->input_height
              << " | outputs=" << impl_->io_num.n_output
              << " | preprocess=" << config_.preprocess_backend
#if defined(AICAM_HAS_RGA) && AICAM_HAS_RGA
              << "(rga_available)"
#else
              << "(rga_unavailable)"
#endif
              << std::endl;

    initialized_ = true;
    return true;
#else
    disabled_reason_ = "RKNN runtime headers/libraries were not found at build time";
    return false;
#endif
}

bool RknnDetector::isEnabled() const {
    return initialized_;
}

std::string RknnDetector::backendName() const {
    return initialized_ ? "rknn" : "disabled";
}

ai::AiResult RknnDetector::infer(const CapturedFrame& frame) {
    ai::AiResult result;
    // 将当前帧序号和采集时间写进结果
    result.frame_sequence = frame.sequence;
    result.capture_time_us = frame.capture_time_us;
    // 返回初始化是否成功
    result.backend = backendName();
    result.note = disabled_reason_;

    if (!initialized_) {
        return result;
    }

#if defined(AICAM_HAS_RKNN) && AICAM_HAS_RKNN
    // scale      原图缩放比例
    // pad_x      左右填充
    // pad_y      上下填充
    // resized_width
    // resized_height
    LetterboxInfo letterbox;

    // 记录预处理时间，单位毫秒
    const auto preprocess_begin = std::chrono::steady_clock::now();

    if (config_.preprocess_backend == "rga") {
#if defined(AICAM_HAS_RGA) && AICAM_HAS_RGA
        std::string rga_error;
        if (!fillLetterboxedRgbWithRga(
                frame,
                impl_->input_width,
                impl_->input_height,
                impl_->rgb_input,
                letterbox,
                rga_error)) {
            if (!impl_->rga_fallback_reported) {
                std::cerr << "RGA 预处理失败，回退 CPU 预处理: " << rga_error << std::endl;
                impl_->rga_fallback_reported = true;
            }
            fillLetterboxedRgb(frame, impl_->input_width, impl_->input_height, impl_->rgb_input, letterbox);
        }
#else
        if (!impl_->rga_fallback_reported) {
            std::cerr << "RGA 未在构建时启用，回退 CPU 预处理。" << std::endl;
            impl_->rga_fallback_reported = true;
        }
        fillLetterboxedRgb(frame, impl_->input_width, impl_->input_height, impl_->rgb_input, letterbox);
#endif
    } else {
        // 1. 读取 frame.nv12
        // 2. NV12 -> RGB
        // 3. 按模型输入尺寸等比例 resize
        // 4. 不足部分用 114 灰色补边
        // 5. 输出到 impl_->rgb_input
        // 6. 把缩放比例和 padding 记录到 letterbox
        fillLetterboxedRgb(frame, impl_->input_width, impl_->input_height, impl_->rgb_input, letterbox);
    }
    // 计算耗时
    const auto preprocess_end = std::chrono::steady_clock::now();
    result.preprocess_ms = std::chrono::duration<double, std::milli>(
        preprocess_end - preprocess_begin
    ).count();

    // 创建rknn_input结构体，传入预处理好的RGB数据
    rknn_input input {};
    input.index = 0;
    input.buf = impl_->rgb_input.data();
    input.size = static_cast<uint32_t>(impl_->rgb_input.size());
    input.pass_through = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;

    // 把输入数据设置到 RKNN runtime
    if (rknn_inputs_set(impl_->context, 1, &input) != RKNN_SUCC) {
        result.note = "rknn_inputs_set failed";
        return result;
    }

    const auto infer_begin = std::chrono::steady_clock::now();
    // 运行模型
    if (rknn_run(impl_->context, nullptr) != RKNN_SUCC) {
        result.note = "rknn_run failed";
        return result;
    }

    // 准备输出结构，创建输出数组
    std::vector<rknn_output> outputs(impl_->io_num.n_output);
    // 逐个设置输出参数，指针、大小、数据类型
    for (uint32_t i = 0; i < impl_->io_num.n_output; ++i) {
        outputs[i].index = i;
        outputs[i].want_float = 1;
        outputs[i].is_prealloc = 0;
    }

    // 取出输出
    if (rknn_outputs_get(impl_->context, impl_->io_num.n_output, outputs.data(), nullptr) != RKNN_SUCC) {
        result.note = "rknn_outputs_get failed";
        return result;
    }
    const auto infer_end = std::chrono::steady_clock::now();

    // 准备 RKNN 性能信息结构体，查询实际推理耗时
    rknn_perf_run perf_run {};
    // 尝试查询推理耗时
    if (rknn_query(impl_->context, RKNN_QUERY_PERF_RUN, &perf_run, sizeof(perf_run)) == RKNN_SUCC) {
        result.inference_ms = static_cast<double>(perf_run.run_duration) / 1000.0;
    } else {
        result.inference_ms = std::chrono::duration<double, std::milli>(
            infer_end - infer_begin
        ).count();
    }

    // 后处理部分
    const auto postprocess_begin = std::chrono::steady_clock::now();

    // 保存所有候选检测框
    std::vector<ai::Detection> detections;
    // 记录解析输出的提示或错误信息
    std::string parse_note;
    // 遍历
    for (size_t output_index = 0; output_index < outputs.size(); ++output_index) {
        // 获取tensnor输出属性
        const auto& attr = impl_->output_attrs[output_index];

        // 拷贝输出维度
        std::vector<uint32_t> dims;
        for (uint32_t i = 0; i < attr.n_dims; ++i) {
            dims.push_back(attr.dims[i]);
        }

        // 有多少行（候选框数量）和多少列（每行属性数量）
        int rows = 0;
        int attrs = 0;
        bool attr_major = false;
        // 根据输出维度推断布局
        if (!inferTensorLayout(dims, rows, attrs, attr_major)) {
            std::ostringstream oss;
            oss << "output " << output_index
                << " layout unsupported, dims=";
            for (size_t i = 0; i < dims.size(); ++i) {
                if (i != 0) {
                    oss << 'x';
                }
                oss << dims[i];
            }
            parse_note = oss.str();
            continue;
        }

        const auto* tensor = static_cast<const float*>(outputs[output_index].buf);
        if (tensor == nullptr) {
            continue;
        }

        // 遍历每个候选框，解析出坐标、类别、置信度等信息
        for (int row = 0; row < rows; ++row) {
            const float b0 = readTensorValue(tensor, rows, attrs, attr_major, row, 0);
            const float b1 = readTensorValue(tensor, rows, attrs, attr_major, row, 1);
            const float b2 = readTensorValue(tensor, rows, attrs, attr_major, row, 2);
            const float b3 = readTensorValue(tensor, rows, attrs, attr_major, row, 3);

            float score = 0.0f;
            int class_id = 0;

            // 按输出属性数量分支
            if (attrs == 5) {
                score = readTensorValue(tensor, rows, attrs, attr_major, row, 4);
                class_id = 0;
            } else if (attrs == 6) {
                score = readTensorValue(tensor, rows, attrs, attr_major, row, 4);
                class_id = static_cast<int>(
                    std::round(readTensorValue(tensor, rows, attrs, attr_major, row, 5))
                );
            } else if (config_.has_objectness && attrs > 5) {
                const float objectness = readTensorValue(tensor, rows, attrs, attr_major, row, 4);
                float best_class_score = -std::numeric_limits<float>::infinity();
                int best_class_id = 0;
                for (int attr_index = 5; attr_index < attrs; ++attr_index) {
                    const float class_score = readTensorValue(
                        tensor,
                        rows,
                        attrs,
                        attr_major,
                        row,
                        attr_index
                    );
                    if (class_score > best_class_score) {
                        best_class_score = class_score;
                        best_class_id = attr_index - 5;
                    }
                }

                class_id = best_class_id;
                score = objectness * best_class_score;
            } else {
                float best_class_score = -std::numeric_limits<float>::infinity();
                int best_class_id = 0;
                for (int attr_index = 4; attr_index < attrs; ++attr_index) {
                    const float class_score = readTensorValue(
                        tensor,
                        rows,
                        attrs,
                        attr_major,
                        row,
                        attr_index
                    );
                    if (class_score > best_class_score) {
                        best_class_score = class_score;
                        best_class_id = attr_index - 4;
                    }
                }

                class_id = best_class_id;
                score = best_class_score;
            }
            // 过滤掉低置信度的框
            if (score < config_.score_threshold) {
                continue;
            }

            float x1 = 0.0f;
            float y1 = 0.0f;
            float x2 = 0.0f;
            float y2 = 0.0f;

            float box0 = b0;
            float box1 = b1;
            float box2 = b2;
            float box3 = b3;

            // 判断坐标是否归一化
            if (looksNormalized(box0, box1, box2, box3)) {
                box0 *= static_cast<float>(impl_->input_width);
                box1 *= static_cast<float>(impl_->input_height);
                box2 *= static_cast<float>(impl_->input_width);
                box3 *= static_cast<float>(impl_->input_height);
            }

            if (config_.box_format == "xyxy") {
                x1 = box0;
                y1 = box1;
                x2 = box2;
                y2 = box3;
            } else {
                x1 = box0 - box2 * 0.5f;
                y1 = box1 - box3 * 0.5f;
                x2 = box0 + box2 * 0.5f;
                y2 = box1 + box3 * 0.5f;
            }

            // 把输入坐标映射回原始摄像头坐标
            x1 = (x1 - static_cast<float>(letterbox.pad_x)) / letterbox.scale;
            y1 = (y1 - static_cast<float>(letterbox.pad_y)) / letterbox.scale;
            x2 = (x2 - static_cast<float>(letterbox.pad_x)) / letterbox.scale;
            y2 = (y2 - static_cast<float>(letterbox.pad_y)) / letterbox.scale;

            // 把坐标限制在原图范围
            x1 = clampFloat(x1, 0.0f, static_cast<float>(frame.width - 1));
            y1 = clampFloat(y1, 0.0f, static_cast<float>(frame.height - 1));
            x2 = clampFloat(x2, 0.0f, static_cast<float>(frame.width - 1));
            y2 = clampFloat(y2, 0.0f, static_cast<float>(frame.height - 1));

            if (x2 <= x1 || y2 <= y1) {
                continue;
            }

            // 把类别分数和坐标等信息保存到Detection 结构体，
            // 并加入候选框列表
            ai::Detection detection;
            detection.class_id = class_id;
            detection.score = score;
            detection.x1 = x1;
            detection.y1 = y1;
            detection.x2 = x2;
            detection.y2 = y2;

            if (class_id >= 0 && class_id < static_cast<int>(impl_->labels.size())) {
                detection.label = impl_->labels[class_id];
            } else {
                detection.label = "class_" + std::to_string(class_id);
            }

            detections.push_back(std::move(detection));
        }
    }

    // 对所有候选框进行非极大值抑制
    // 解决同一目标多个框重叠的问题，保留最有可能的框
    // 1. 按 score 从高到低排序
    // 2. 逐个选择候选框
    // 3. 如果同类别框和已选框 IoU 超过 nms_threshold，就丢掉
    // 4. 最多保留 max_results 个结果
    result.detections = applyNms(
        std::move(detections),
        config_.nms_threshold,
        config_.max_results
    );
    // 如果最终有检测结果，就清空解析提示
    if (!result.detections.empty()) {
        parse_note.clear();
    }
    // 记录耗时
    result.postprocess_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - postprocess_begin
    ).count();
    result.valid = true;
    result.note = parse_note;
    // 释放内存
    rknn_outputs_release(impl_->context, impl_->io_num.n_output, outputs.data());
    return result;
#else
    return result;
#endif
}
