# 让 CMake 找到并编译你的自定义源文件
zephyr_sources_ifdef(CONFIG_ZMK_RGB_UNDERGLOW src/custom_status_led.c)
