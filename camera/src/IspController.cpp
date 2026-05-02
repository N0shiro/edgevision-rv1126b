#include "IspController.h"

#include <cstring>
#include <iostream>

extern "C" {
#include <rkaiq/uAPI2/rk_aiq_user_api2_imgproc.h>
#include <rkaiq/uAPI2/rk_aiq_user_api2_sysctl.h>
}

IspController::IspController(int sensor_id_value, int fps_value, std::string iq_dir_value)
    : sensor_id(sensor_id_value),
      fps(fps_value),
      iq_dir(std::move(iq_dir_value)),
      ctx(nullptr),
      running(false) {}

IspController::~IspController() {
    stop();
}

bool IspController::start() {
    if (running) {
        return true;
    }

    rk_aiq_static_info_t static_info;
    std::memset(&static_info, 0, sizeof(static_info));

    XCamReturn ret = rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(sensor_id, &static_info);
    if (ret != XCAM_RETURN_NO_ERROR) {
        std::cerr << " AIQ 枚举传感器失败: " << ret << std::endl;
        return false;
    }

    if (static_info.sensor_info.sensor_name[0] == '\0') {
        std::cerr << " AIQ 未找到有效传感器名称" << std::endl;
        return false;
    }

    ctx = rk_aiq_uapi2_sysctl_init(
        static_info.sensor_info.sensor_name,
        iq_dir.c_str(),
        nullptr,
        nullptr
    );
    if (ctx == nullptr) {
        std::cerr << " AIQ 初始化失败, IQ 目录: " << iq_dir << std::endl;
        return false;
    }

    ret = rk_aiq_uapi2_sysctl_prepare(ctx, 0, 0, RK_AIQ_WORKING_MODE_NORMAL);
    if (ret != XCAM_RETURN_NO_ERROR) {
        std::cerr << " AIQ prepare 失败: " << ret << std::endl;
        stop();
        return false;
    }

    ret = rk_aiq_uapi2_sysctl_start(ctx);
    if (ret != XCAM_RETURN_NO_ERROR) {
        std::cerr << " AIQ start 失败: " << ret << std::endl;
        stop();
        return false;
    }

    opMode_t wb_mode = OP_AUTO;
    ret = rk_aiq_uapi2_setWBMode(ctx, wb_mode);
    if (ret != XCAM_RETURN_NO_ERROR) {
        std::cerr << " AIQ 自动白平衡设置失败: " << ret << std::endl;
    }

    frameRateInfo_t frame_rate;
    std::memset(&frame_rate, 0, sizeof(frame_rate));
    frame_rate.mode = OP_MANUAL;
    frame_rate.fps = static_cast<float>(fps);
    ret = rk_aiq_uapi2_setFrameRate(ctx, frame_rate);
    if (ret != XCAM_RETURN_NO_ERROR) {
        std::cerr << " AIQ 帧率设置失败: " << ret << std::endl;
    }

    running = true;
    std::cout << " AIQ 已启动 | sensor_id: " << sensor_id
              << " | sensor_name: " << static_info.sensor_info.sensor_name
              << " | iq_dir: " << iq_dir << std::endl;
    return true;
}

void IspController::stop() {
    if (ctx != nullptr) {
        if (running) {
            rk_aiq_uapi2_sysctl_stop(ctx, false);
        }
        rk_aiq_uapi2_sysctl_deinit(ctx);
    }

    ctx = nullptr;
    running = false;
}

bool IspController::isRunning() const {
    return running;
}
