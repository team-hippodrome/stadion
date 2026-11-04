"""Platform helpers shared by the replay and reference runners."""
import json
import os
import shutil
import subprocess

BACKENDS = {"directx": (1, "DirectX 11"), "opengl": (2, "OpenGL"), "metal": (3, "Metal"), "vulkan": (4, "Vulkan")}


def process_options():
    if os.name == "nt":
        return {"creationflags": subprocess.CREATE_NO_WINDOW | subprocess.IDLE_PRIORITY_CLASS}
    return {"preexec_fn": low_priority}


def low_priority():
    os.setpriority(os.PRIO_PROCESS, 0, 15)


def backend_home(env, directory, backend=None):
    backend = backend or env.get("PORT_TEST_BACKEND")
    if not backend:
        return
    ident, name = BACKENDS[backend]
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "pokestadium.cfg.json"), "w") as f:
        json.dump({"Window": {"Backend": {"Id": ident, "Name": name}}}, f)
    env["SHIP_HOME"] = directory


def actual_backend(directory):
    try:
        with open(os.path.join(directory, "pokestadium.cfg.json")) as f:
            return json.load(f)["Window"]["Backend"]["Id"]
    except (OSError, ValueError, KeyError):
        return None


def reference_rom(source, target):
    if os.path.lexists(target):
        return target
    if os.name == "nt":
        shutil.copyfile(source, target)  # symlinks require privileges on Windows
    else:
        os.symlink(source, target)
    return target


def reference_exe(build):
    return os.path.join(build, "ares_ref.exe" if os.name == "nt" else "ares_ref")
