zephyr_library()
zephyr_library_sources(status_led.c)

# Module sources include ZMK headers (zmk/*.h), which live in the ZMK app
# include dir and are not on the default include path for module targets.
zephyr_include_directories(${WEST_TOPDIR}/zmk/app/include)
