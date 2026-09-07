idf_component_register(
    SRCS "main.cpp" "input.cpp" "render.cpp" "marquee.cpp" "egg.cpp"
    INCLUDE_DIRS "." "roms"
    REQUIRES driver esp_timer esp_partition display imu emu audio_hal helix_mp3 app_update esp_partition
)
