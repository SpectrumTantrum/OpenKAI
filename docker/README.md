# OpenKAI Docker build environment

Reproducible, hardware-free build + smoke-test of OpenKAI in an Ubuntu 24.04
container. Builds natively for the host architecture (arm64 on Apple Silicon),
so `CMAKE_BUILD_TYPE=Release` with `-O3 -march=native` is safe.

## Prerequisites

- Docker Desktop (running). Verify with `docker info`.
- Run all commands from the **repo root**.

## The three commands

```bash
# 1. build  — compile OpenKAI (builds the image; fails if compilation fails)
docker compose -f docker/compose.yaml build openkai

# 2. test   — headless smoke test (boots the binary, runs module lifecycle)
docker compose -f docker/compose.yaml run --rm openkai

# 3. shell  — interactive dev shell, repo bind-mounted at /work
docker compose -f docker/compose.yaml run --rm dev
```

### Incremental dev builds (the `shell` service)

The repo is bind-mounted at `/work`; the CMake build directory lives in a
**named Docker volume** (`openkai-build`) mounted at `/work/build`, so Linux
build artifacts never pollute the macOS host tree. Inside the shell:

```bash
cmake -S /work -B /work/build -DCMAKE_BUILD_TYPE=Release \
  -DWITH_IO=ON -DWITH_PROTOCOL=ON -DWITH_NAVIGATION=ON -DWITH_SENSOR=ON \
  -DWITH_ACTUATOR=ON -DWITH_NET=ON -DWITH_CONTROL=ON -DWITH_FILTER=ON \
  -DWITH_STATE=ON -DWITH_SOLVER=ON -DWITH_UNIVERSE=ON -DWITH_UI=ON \
  -DWITH_FILE=ON -DWITH_APMAVLINK=ON -DWITH_AUTOPILOT_DRIVE=ON
cmake --build /work/build -j"$(nproc)"

# smoke test against the freshly built binary
SMOKE_SECONDS=6 bash docker/smoke.sh
```

Reset the build dir with `docker volume rm docker_openkai-build`.

## The smoke test

`docker/smoke.sh` runs `./build/OpenKAI docker/smoke.json` for a few seconds
under `timeout`, then asserts the module-manager lifecycle log lines
(`Instance created` / `Initialized` / `Linked` / `Started`) appeared without a
crash. `docker/smoke.json` enables a single headless `_UDP` module (binds a
local UDP socket, needs no hardware and no display). Tunables:
`OPENKAI_BIN`, `OPENKAI_CFG`, `SMOKE_SECONDS`.

Most stock configs in `jsonCfg/` (e.g. `helloOK.json`) require a camera/OpenCV
or real hardware, so they are unsuitable for a headless smoke test — hence the
dedicated minimal config.

## What is enabled / disabled, and why

All proprietary/SDK options (`USE_CUDA`, `USE_REALSENSE`, `USE_ORBBEC`,
`USE_SCEPTER_SDK`, `USE_XDYNAMICS`, `USE_OPEN3D`, `USE_OPENCV`, ...) are **OFF**.
`USE_GLOG` stays ON (its default). The image enables the maximal set of
`WITH_*` module groups that build with **no external SDK** — they depend only
on apt libraries or the vendored sources in `src/Dependencies/`:

| Enabled | Notes |
|---|---|
| `WITH_IO` | serial/TCP/UDP/file IO (wsServer left off — external lib) |
| `WITH_PROTOCOL` | vendored libmodbus + MAVLink c_library_v2; SocketCAN headers |
| `WITH_NAVIGATION` | vendored minmea |
| `WITH_SENSOR` | vendored SensorFusion |
| `WITH_ACTUATOR` | motor drivers (Feetech left off — see below) |
| `WITH_NET` | needs `libevent-dev` (event/event_core) |
| `WITH_CONTROL`, `WITH_FILTER`, `WITH_STATE`, `WITH_SOLVER` | pure C++ |
| `WITH_UNIVERSE` | OpenCV paths are `#ifdef USE_OPENCV`-guarded |
| `WITH_UI` | console/websocket UI; OpenCV window paths guarded |
| `WITH_FILE`, `WITH_APMAVLINK`, `WITH_AUTOPILOT_DRIVE` | |

**Left OFF because they are broken on this branch (pre-existing repo bugs, not
Docker issues):**

- `WITH_SWARM` — `src/Swarm/_SwarmCtrl.{h,cpp}` (and `_APmavlink_swarm`,
  `Module.{h,cpp}`) `#include "_SwarmSearch.h"`, but **that header does not
  exist anywhere in the repo**. `fatal error: ../../Swarm/_SwarmSearch.h: No
  such file or directory`.
- `USE_FEETECH` — `src/Actuator/_Feetech.{h,cpp}` are out of sync with the
  current `_ActuatorBase` API: undeclared `ACTUATOR_AXIS`, `m_pA`, `m_vAxis`,
  and misused `IF__` / `NULL__` macros.
- `WITH_TEST` — `test/_TestBase.cpp` and `test/Protocol/_TestJSON.cpp` call an
  old `_Thread::start(fn, ctx)` signature and use undefined JSON helpers
  (`JO`, `object`).

Apt toolchain/libraries installed (see `Dockerfile` `deps` stage):
`build-essential cmake git pkg-config uuid-dev libncurses-dev libssl-dev
libuvc-dev libusb-1.0-0-dev libgsl-dev libeigen3-dev libgoogle-glog-dev
libevent-dev`.

## Enabling OpenCV / SDK options later

`USE_OPENCV=ON` requires **OpenCV 5.0.0** built from source with contrib
(`find_package(OpenCV 5 REQUIRED CONFIG)`, `OpenCV_DIR=/usr/local/lib/cmake/opencv5`;
recipe in `docs/opencv.md`). A ready-made `opencv` stage in the `Dockerfile`
does exactly this and then compiles OpenKAI with
`USE_OPENCV=ON -DUSE_OPENCV_CONTRIB=ON` plus `WITH_VISION`/`WITH_DETECTOR`.
It is **provided but long-running** (OpenCV-from-source, ~a few minutes to
configure/download + tens of minutes to compile) and is not part of the default
build:

```bash
docker build -f docker/Dockerfile --target opencv -t openkai:opencv .
```

**Status (verified):** the OpenCV 5.0.0 + contrib build/install step succeeds
(all `libopencv_*.so.5.0.0` installed, contrib modules included). The
subsequent OpenKAI-with-OpenCV compile then hits a **pre-existing repo bug**,
not a Docker one:

- `src/Vision/FrameGPU.h` declares `GpuMat m_matG;` (i.e. `cv::cuda::GpuMat`)
  unconditionally, but the OpenCV CUDA headers are only included under
  `USE_CUDA`. So `USE_OPENCV=ON` with `USE_CUDA=OFF` fails with
  `field 'm_matG' has incomplete type 'cv::cuda::GpuMat'`. `FrameGPU.h` is
  pulled in transitively via `Vision/Frame.h` whenever `USE_OPENCV` is defined.

To make the `opencv` target link, either build with a CUDA-capable OpenCV and
`USE_CUDA=ON`, or guard the `GpuMat` member in `FrameGPU.h` behind `#ifdef
USE_CUDA` (a one-line source change intentionally not made here).

Proprietary SDKs (RealSense, Orbbec, Scepter, XDynamics, CUDA, Open3D, ONNX
Runtime, ...) need vendor packages installed into the image and their matching
`USE_*` / `WITH_*` flags turned on — add the install steps to a new stage
derived from `deps` and extend the `cmake` option list.
