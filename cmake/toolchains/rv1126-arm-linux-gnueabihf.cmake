# 兼容入口：
# 之前误把 RV1126B / Luckfox Aura 当作 32 位 armhf 目标。
# 当前设备实际运行环境是 aarch64，因此这里直接转到新的 aarch64 工具链文件。
include("${CMAKE_CURRENT_LIST_DIR}/rv1126b-aarch64-linux-gnu.cmake")
