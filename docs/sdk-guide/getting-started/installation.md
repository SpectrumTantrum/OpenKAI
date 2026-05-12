# Installation

This page shows the static build shape for OpenKAI on Linux: install the base dependencies, configure CMake, choose which module families to compile, and understand the difference between source-inclusion flags such as `WITH_ROS` and dependency flags such as `USE_ROS`.

## Minimal Build Commands

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

The default build type is `Debug`, set in `CMakeLists.txt:3`. The project requires CMake 3.18 or newer at `CMakeLists.txt:1`, uses C++17 at `CMakeLists.txt:64`, and always links core libraries such as pthread, Eigen, uuid, gsl, ncurses, OpenSSL, uvc, and usb-1.0 at `CMakeLists.txt:85`.

The existing setup notes list Ubuntu-style package installation commands in `docs/setup.md:11`. Treat those as environment preparation, not as OpenKAI runtime configuration.

## Build With The Common Module Set

The minimal build compiles the base runtime, module manager, IPC, primitive types, `_Console`, and file support. You can see these always-included source groups in `CMakeLists.txt:87`, `CMakeLists.txt:92`, `CMakeLists.txt:96`, `CMakeLists.txt:99`, and `CMakeLists.txt:102`.

For a practical first camera/window app, configure with the common module set:

```bash
cmake -DWITH_DEFAULT_MODULES=ON ..
make -j$(nproc)
```

`WITH_DEFAULT_MODULES` is a convenience flag that turns on many module families at once. It forces `USE_OPENCV`, `USE_OPENCV_CONTRIB`, and module families such as actuator, autopilot, control, detector, IO, navigation, protocol, sensor, UI, universe, and vision. See `CMakeLists.txt:106`.

This matters for `jsonCfg/helloOK.json`: it uses `_Camera` and `_WindowCV`, so the binary must include the vision and UI module code that defines those classes.

## Understand `WITH_*` And `USE_*`

A `WITH_*` flag usually says "compile this OpenKAI module family." For example, `WITH_VISION` controls vision modules, `WITH_UI` controls UI modules, and `WITH_ROS` adds source files under `src/ROS/`.

A `USE_*` flag usually says "enable support for a library, SDK, or backend used by some modules." For example, `USE_OPENCV` enables OpenCV-dependent modules, `USE_REALSENSE` enables RealSense-specific pieces, and `USE_ROS` adds ROS2 package dependencies.

The pattern is visible in `CMakeLists.txt`: `WITH_ROS` adds `src/ROS/*.cpp` files at `CMakeLists.txt:348`, while `USE_ROS` finds `ament_cmake`, `rclcpp`, and ROS message packages at `CMakeLists.txt:688`.

For ROS2 bridge work, use both:

```bash
cmake -DWITH_ROS=ON -DUSE_ROS=ON ..
```

`WITH_ROS` makes the OpenKAI ROS bridge module classes available. `USE_ROS` makes the target depend on ROS2 libraries and emits the ament package wiring.

## Build As A Library

OpenKAI normally builds an executable named `OpenKAI`. That happens through `add_executable(OpenKAI ...)` at `CMakeLists.txt:681`.

To build the SDK as a library instead:

```bash
cmake -DBUILD_AS_LIB=ON ..
make -j$(nproc)
sudo make install
```

When `BUILD_AS_LIB` is enabled, the build creates a library target instead of the executable path. Installation copies the library and headers according to the rules at `CMakeLists.txt:705` and `CMakeLists.txt:708`.

## What Can Go Wrong

If a JSON config names a module class that was not compiled into the binary, the module factory cannot create it. The config may still parse, but the instance will not exist for later lifecycle stages.

If `USE_ROS` is enabled without a ROS2 environment that provides `ament_cmake`, `rclcpp`, `std_msgs`, `sensor_msgs`, and `nav_msgs`, CMake configuration will fail because those packages are required at `CMakeLists.txt:690`.

If you build only the minimal executable and then run a config that expects OpenCV-backed camera or window modules, the class names in that config will not match compiled module registrations.

Next: [Your First Run](your-first-run.md)
