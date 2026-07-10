Import("env")

from os.path import join


packages_dir = env.subst("$PROJECT_PACKAGES_DIR")
arduino_dir = join(packages_dir, "framework-arduinoespressif32")
arduino_libs_dir = join(packages_dir, "framework-arduinoespressif32-libs")
idf_dir = join(packages_dir, "framework-espidf")

env.Append(
    CPPPATH=[
        join(arduino_dir, "libraries", "FS", "src"),
        join(arduino_dir, "libraries", "Network", "src"),
        join(arduino_libs_dir, "esp32s3", "include", "soc", "esp32s3", "register"),
        join(idf_dir, "components", "esp_event", "include"),
        join(idf_dir, "components", "esp_netif", "include"),
        join(idf_dir, "components", "esp_timer", "include"),
        join(idf_dir, "components", "mbedtls", "port", "include"),
        join(idf_dir, "components", "mbedtls", "mbedtls", "include"),
        join(idf_dir, "components", "pthread", "include"),
        join(env.subst("$PROJECT_DIR"), "managed_components", "espressif__mdns", "include"),
    ]
)

if env.subst("$PIOENV") in {
    "muse_radio_radio_and_more",
    "muse_radio_spotify_poc",
    "muse_radio_usb_display_poc",
}:
    project_dir = env.subst("$PROJECT_DIR")
    cspot_dir = join(project_dir, "third_party", "cspot", "cspot")
    bell_dir = join(cspot_dir, "bell")
    cspot_includes = [
        join(cspot_dir, "include"),
        join(project_dir, "components", "cspot_component", "generated"),
        join(bell_dir, "main", "audio-codec", "include"),
        join(bell_dir, "main", "audio-dsp", "include"),
        join(bell_dir, "main", "audio-sinks", "include"),
        join(bell_dir, "main", "io", "include"),
        join(bell_dir, "main", "utilities", "include"),
        join(bell_dir, "main", "platform"),
        join(bell_dir, "external", "nanopb"),
        join(bell_dir, "external", "cJSON"),
        join(bell_dir, "external", "civetweb", "include"),
        join(bell_dir, "external", "nlohmann_json", "include"),
    ]
    env.Append(CXXFLAGS=["-iquote" + path for path in cspot_includes])
