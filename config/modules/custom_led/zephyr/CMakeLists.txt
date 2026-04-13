zephyr_library()
zephyr_library_sources_ifdef(CONFIG_CUSTOM_LED custom_led.c)
