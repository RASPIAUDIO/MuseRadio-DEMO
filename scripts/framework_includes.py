Import("env")

from os.path import join

packages_dir = env.subst("$PROJECT_PACKAGES_DIR")
arduino_libraries = join(packages_dir, "framework-arduinoespressif32", "libraries")
env.Append(
    CPPPATH=[
        join(arduino_libraries, "FS", "src"),
        join(arduino_libraries, "Network", "src"),
    ]
)
