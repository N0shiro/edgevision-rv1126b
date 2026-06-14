#pragma once

#include <string>

struct rk_aiq_sys_ctx_s;
using rk_aiq_sys_ctx_t = rk_aiq_sys_ctx_s;

class IspController {
public:
    IspController(int sensor_id, int fps, std::string iq_dir, float btnr_strength);
    ~IspController();

    bool start();
    void stop();
    bool isRunning() const;

private:
    int sensor_id;
    int fps;
    std::string iq_dir;
    float btnr_strength;
    rk_aiq_sys_ctx_t* ctx;
    bool running;
};
