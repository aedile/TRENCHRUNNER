idf_component_register(
    SRCS "main.cpp" "input.cpp" "render.cpp"
    INCLUDE_DIRS "." "roms"
    REQUIRES driver esp_timer display imu emu audio_hal
)
