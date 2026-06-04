# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Overview

OpenKAI is a C++17 framework for unmanned vehicles and robotics. It runs as a single executable (`OpenKAI`) driven by a JSON config file that specifies which modules to instantiate, how they are linked, and per-module thread rates. There is no separate test runner — "tests" are JSON-selected modules compiled into the same binary.

The entire codebase lives in `namespace kai`. Module classes are prefixed with an underscore by convention (e.g. `_Camera`, `_APmavlink_base`); plain names are used for value/helper classes (`BASE`, `Module`, `ModuleMgr`, `JsonCfg`).

## Build

The build is heavily option-gated; nothing optional is compiled in unless its `WITH_*` and `USE_*` flags are toggled on. Mandatory deps: pthread, Eigen3 (>=3.1.0), uuid, gsl, ncurses, ssl/crypto, uvc, usb-1.0; `glog` is on by default via `USE_GLOG`.

```bash
mkdir build && cd build

# Minimal exe (Base + Arithmetic + Module + IPC + Primitive only)
cmake ..
make -j$(nproc)

# Sensible default app (vision, sensors, autopilot, etc. — requires OpenCV & contrib installed)
cmake -DWITH_DEFAULT_MODULES=ON ..
make -j$(nproc)

# Release optimization (default is Debug)
cmake -DCMAKE_BUILD_TYPE=Release ..

# As a shared library + installed headers
cmake -DBUILD_AS_LIB=ON ..
sudo make install   # → /usr/local/lib + /usr/local/include/OpenKAI

# Curses UI to browse all options
ccmake ..
```

`WITH_DEFAULT_MODULES=ON` force-sets a broad set of `WITH_*` plus `USE_OPENCV` / `USE_OPENCV_CONTRIB`; everything else (CUDA, Realsense, Open3D, Orbbec, ROS, Jetson Inference, TF-Lite, Qiskit, M4RI, WSServer, GUI, Feetech, XArm, Dynamixel, FastLivo, Chilitags, MathGL, Scepter, XDynamics) is opt-in via the matching `USE_*` flag.

ROS2 builds: pass `-DUSE_ROS=ON`; CMake then requires `ament_cmake`, `rclcpp`, `std_msgs`, `sensor_msgs`, `nav_msgs` and emits an ament package — build via `colcon` instead of bare `make`.

OS setup, dependency install instructions, and platform-specific notes are in `docs/setup.md` (Ubuntu/Jetson/Pi). The repo targets Linux only — `darwin` builds are not supported.

## Run

```bash
./OpenKAI <path/to/config.json>      # SIGINT → stopAll() → clean exit
```

`jsonCfg/` contains full apps (e.g. `helloOK.json`, `_Camera.json`, `apMavlink_RTCM.json`); `*.kiss` files are partial fragments referenced via `vInclude` from a parent JSON. `sh/startup/ok.sh` shows the typical "loop-relaunch" deployment pattern used on Jetson/Pi targets.

## Architecture

The framework is essentially a **JSON-configured DAG of threaded modules**:

1. `src/main.cpp` instantiates `ModuleMgr`, calls `parseJsonFile → createAll → initAll → linkAll → startAll → waitForComplete`. Each phase is a separate pass over every configured module so that `link()` can safely resolve references to other modules created in the previous phase.

2. **JSON layout** (`src/Module/JsonCfg.cpp`): the top-level `APP` object holds app-wide settings (`appName`, `bLog`, `bStdErr`, `vInclude` for splitting configs across files). Every other top-level key is a module instance: `class` selects the C++ type to instantiate, `bON` toggles it, and a nested `thread` object (with `FPS`) configures its worker. Cross-module references use `vBASE` arrays of module names — these are looked up in `ModuleMgr::findModule()` during `link()`.

3. **Class factory** (`src/Module/Module.{h,cpp}`): `Module::createInstance(name)` is a long if-else chain of `ADD_MODULE(x)` macro expansions. The macro registry in `Module.cpp` and the includes in `Module.h` are both guarded by the same `WITH_*` / `USE_*` `#ifdef`s as `CMakeLists.txt` — **all three must be edited in lockstep** when adding or gating a module.

4. **Base hierarchy** (`src/Base/`):
   - `BASE` — names, config, virtual `init/link/start/check/pause/resume/stop`.
   - `_Thread` — pthread-backed worker with explicit state machine (`thread_run/sleep/pause/resume/stop`), FPS regulation via `autoFPS()`, and a `vRunThread` list for waking sibling workers.
   - `_ModuleBase` — combines a `_Thread` with the `ON_PAUSE` / `ON_RESUME` hooks; most domain modules derive from this and implement an `update()` loop that calls `m_pT->sleepT(...)` then `m_pT->autoFPS()` each tick.

5. **Domain subtrees under `src/`** — each is a sibling directory gated by one CMake flag, so the dependency cone is shallow:
   `3D`, `Actuator`, `Autopilot/APmavlink`, `Autopilot/Drive`, `Compute/OpenCL`, `Control`, `Detector`, `DNN/{JetsonInference,TensorFlowLite}`, `Filter`, `IO`, `IPC`, `Navigation`, `Net`, `Protocol`, `ROS`, `SLAM`, `Sensor/{Distance,LiDAR}`, `Solver`, `State`, `Swarm`, `Tracker`, `UI`, `Universe`, `Vision/{RGBD,ImgFilter}`. Vendored third-party C/C++ lives in `src/Dependencies/` (mavlink `c_library_v2`, `libmodbus`, `minmea`, `SensorFusion`, Feetech SDK).

6. **Conventions enforced by `src/Base/macro.h`**:
   - Logging routes through `LOG_I/E/F` and toggles between glog and `printf` at compile time via `USE_GLOG`.
   - Guard-and-return idioms `IF_F(x)` / `IF_N(x)` / `IF_T(x)` / `IF_CONT(x)` are pervasive — prefer them over hand-rolled `if(!x) return false;` to match the codebase style.
   - `jK(json, key)` and `jKv(json, key, var)` are the standard ways to pull values from `nlohmann::json` config.

## Adding a module

Three coordinated edits:

1. Create `src/<Domain>/_YourThing.{h,cpp}` deriving from `_ModuleBase` (or `BASE` for non-threaded helpers). Implement `init(json)`, `link(json, ModuleMgr*)`, `start()`, and a private `update()` loop if threaded.
2. In `CMakeLists.txt`, append the new `.cpp` to `OpenKAI_cpp` inside the appropriate `if(WITH_<DOMAIN>) ... endif()` block (and any nested `USE_*` block if it needs a specific library).
3. In `src/Module/Module.h`, add an `#include` of your header under the matching `#ifdef`. In `src/Module/Module.cpp`, add `ADD_MODULE(_YourThing);` under the same `#ifdef`.

The class is then instantiable from JSON by setting `"class": "_YourThing"`.

## Tests

`WITH_TEST=ON` compiles `test/_TestBase.cpp` and its subdirectories (currently `test/IO/` and `test/Protocol/`) directly into the `OpenKAI` binary as additional modules. There is no `ctest`, no test runner, and no per-test target — exercise them by writing a JSON config that instantiates the relevant `_Test*` class.
