# syntax=docker/dockerfile:1
#
# OpenKAI build environment (Ubuntu 24.04, native arch — arm64 on Apple Silicon).
#
# Stages:
#   deps    - apt toolchain + SDK-free library set (also used as the dev shell image)
#   build   - compiles OpenKAI in Release with the maximal SDK-free module set
#   opencv  - (stretch, long-running) OpenCV 5.0.0 + contrib from source, then
#             OpenKAI with USE_OPENCV=ON. Not built by default.

# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS deps
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        pkg-config \
        ca-certificates \
        uuid-dev \
        libncurses-dev \
        libssl-dev \
        libuvc-dev \
        libusb-1.0-0-dev \
        libgsl-dev \
        libeigen3-dev \
        libgoogle-glog-dev \
        libevent-dev \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
FROM deps AS build
WORKDIR /src
COPY . /src

# Maximal module set that needs NO proprietary/external SDK and actually
# compiles on this branch. All USE_* SDK flags stay OFF (default). Enabled
# WITH_* groups depend only on vendored sources (src/Dependencies/*) or the
# apt libraries installed above.
#
# Deliberately left OFF because they are broken on this branch (see docker/README.md):
#   WITH_SWARM    -> references missing header src/Swarm/_SwarmSearch.h
#   USE_FEETECH   -> src/Actuator/_Feetech.{h,cpp} out of sync with _ActuatorBase API
#   WITH_TEST     -> test/ modules use an old _Thread::start signature / JSON helpers
RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DWITH_IO=ON \
        -DWITH_PROTOCOL=ON \
        -DWITH_NAVIGATION=ON \
        -DWITH_SENSOR=ON \
        -DWITH_ACTUATOR=ON \
        -DWITH_NET=ON \
        -DWITH_CONTROL=ON \
        -DWITH_FILTER=ON \
        -DWITH_STATE=ON \
        -DWITH_SOLVER=ON \
        -DWITH_UNIVERSE=ON \
        -DWITH_UI=ON \
        -DWITH_FILE=ON \
        -DWITH_APMAVLINK=ON \
        -DWITH_AUTOPILOT_DRIVE=ON \
    && cmake --build build -j"$(nproc)"

# Default command runs the headless smoke test.
CMD ["bash", "docker/smoke.sh"]

# ---------------------------------------------------------------------------
# Stretch target: OpenCV 5.0.0 + contrib from source (per docs/opencv.md).
# Long-running (typically 30-60 min on Apple Silicon). Build explicitly with:
#   docker build -f docker/Dockerfile --target opencv -t openkai:opencv .
FROM deps AS opencv
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        wget unzip \
        libgtk-3-dev \
        libavcodec-dev libavformat-dev libswscale-dev libv4l-dev \
        libjpeg-dev libpng-dev libtiff-dev \
        libtbb-dev \
        gstreamer1.0-plugins-base libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/ocv
RUN wget -q -O opencv.zip https://github.com/opencv/opencv/archive/refs/tags/5.0.0.zip \
    && wget -q -O contrib.zip https://github.com/opencv/opencv_contrib/archive/refs/tags/5.0.0.zip \
    && unzip -q opencv.zip && unzip -q contrib.zip && rm -f opencv.zip contrib.zip
RUN cmake -S opencv-5.0.0 -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DOPENCV_EXTRA_MODULES_PATH=/opt/ocv/opencv_contrib-5.0.0/modules \
        -DBUILD_LIST=core,imgproc,imgcodecs,highgui,videoio,calib,objdetect,features,dnn,bgsegm,tracking,xobjdetect \
        -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF \
        -DWITH_CUDA=OFF \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build \
    && ldconfig \
    && rm -rf /opt/ocv

WORKDIR /src
COPY . /src
RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DUSE_OPENCV=ON \
        -DUSE_OPENCV_CONTRIB=ON \
        -DWITH_IO=ON \
        -DWITH_PROTOCOL=ON \
        -DWITH_NAVIGATION=ON \
        -DWITH_SENSOR=ON \
        -DWITH_ACTUATOR=ON \
        -DWITH_NET=ON \
        -DWITH_CONTROL=ON \
        -DWITH_FILTER=ON \
        -DWITH_STATE=ON \
        -DWITH_SOLVER=ON \
        -DWITH_UNIVERSE=ON \
        -DWITH_UI=ON \
        -DWITH_FILE=ON \
        -DWITH_APMAVLINK=ON \
        -DWITH_AUTOPILOT_DRIVE=ON \
        -DWITH_DETECTOR=ON \
        -DWITH_VISION=ON \
    && cmake --build build -j"$(nproc)"
CMD ["bash", "docker/smoke.sh"]
