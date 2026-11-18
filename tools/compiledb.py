# PlatformIO pre-build script: keeps compile_commands.json (clangd's include paths) fresh, so the
# editor never shows "'NimBLEDevice.h' file not found" after a library change or a fresh clone.
# Regenerates only when the file is missing or older than platformio.ini or the src/ file list;
# otherwise it costs nothing. Runs `pio run -t compiledb` as its own process because PlatformIO
# only sets up the database (absolute toolchain paths, no @response files) for that target.
import os
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO/SCons)

if "compiledb" not in COMMAND_LINE_TARGETS:  # noqa: F821
    project = env.subst("$PROJECT_DIR")
    db = os.path.join(project, "compile_commands.json")
    inputs = [os.path.join(project, "platformio.ini"), os.path.join(project, "src")]  # dir mtime: files added/removed
    if not os.path.exists(db) or os.path.getmtime(db) < max(os.path.getmtime(p) for p in inputs):
        print("compile_commands.json is stale, regenerating for clangd")
        subprocess.run([env.subst("$PYTHONEXE"), "-m", "platformio", "run", "-t", "compiledb",
                        "-d", project, "-e", env.subst("$PIOENV")], check=False)
