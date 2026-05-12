# Enable ROS2 Bridge

This guide explains the OpenKAI-side mechanics for enabling the optional ROS2 bridge modules. A **bridge module** is an adapter that lets OpenKAI exchange data with another runtime instead of only using in-process OpenKAI modules.

## The Two Flags

OpenKAI has two relevant CMake flags:

```bash
cmake -DWITH_ROS=ON -DUSE_ROS=ON ..
```

`WITH_ROS` controls whether OpenKAI compiles the ROS bridge source files. It is declared at `CMakeLists.txt:52` and adds files from `src/ROS/` at `CMakeLists.txt:348`.

`USE_ROS` controls whether the build finds and links ROS2 dependencies. It finds `ament_cmake`, `rclcpp`, `std_msgs`, `sensor_msgs`, and `nav_msgs` at `CMakeLists.txt:688`.

Use both when you want a working ROS2 bridge module.

## What Gets Added To The Factory

When `WITH_ROS` is enabled, `Module.h` includes the ROS bridge header at `src/Module/Module.h:198`.

The factory registers `_ROS_fastLio` at `src/Module/Module.cpp:208`.

That means a config can instantiate the bridge with:

```json
"fastLio": {
  "class": "_ROS_fastLio",
  "bON": true,
  "thread": { "name": "thread", "class": "_Thread", "FPS": 30 },
  "threadROS": { "name": "threadROS", "class": "_Thread", "FPS": 30 },
  "node": {
    "topicPC2": "Laser_map",
    "topicOdom": "Odometry",
    "topicPath": "path"
  }
}
```

This snippet is illustrative. Match topic names to the ROS2 system you are connecting to.

## What `_ROS_fastLio` Does

`_ROS_fastLio` is an OpenKAI module that owns both an OpenKAI update loop and a ROS node.

During `init()`, it creates a second thread named `threadROS`, calls `rclcpp::init`, creates a `ROS_fastLio` node, and passes it the nested `node` JSON object. See `src/ROS/_ROS_fastLio.cpp:26`.

During `start()`, it starts the normal OpenKAI update thread and also starts the ROS thread. See `src/ROS/_ROS_fastLio.cpp:54`.

The ROS thread creates subscriptions and spins the ROS node at `src/ROS/_ROS_fastLio.cpp:90`.

## Configure Topics

The nested `node` object contains topic names:

```json
"node": {
  "topicPC2": "Laser_map",
  "topicOdom": "Odometry",
  "topicPath": "path"
}
```

`ROS_fastLio::init()` reads these fields at `src/ROS/ROS_fastLio.cpp:12`.

`ROS_fastLio::createSubscriptions()` subscribes to point cloud, odometry, and path messages when the corresponding topic names are not empty. See `src/ROS/ROS_fastLio.cpp:28`.

The node class itself derives from `rclcpp::Node` and names the node `openkai_node` at `src/ROS/ROS_fastLio.h:29`.

## Optional OpenKAI Links

`_ROS_fastLio::link()` can connect ROS data back into OpenKAI objects. When `WITH_3D` is enabled, it reads `_PCframe` and resolves that OpenKAI module name at `src/ROS/_ROS_fastLio.cpp:43`.

That gives the bridge a place to copy or expose received point cloud data inside OpenKAI.

If the target OpenKAI module is absent or disabled, that pointer is not available.

## What Goes Wrong

If `WITH_ROS` is off, `_ROS_fastLio` is not compiled into the factory.

If `USE_ROS` is off, the ROS2 dependencies are not wired into the target.

If the local environment does not provide the required ROS2 packages, CMake configuration fails at the `find_package(...)` stage.

If topic names are empty, `ROS_fastLio::createSubscriptions()` simply skips those subscriptions because it checks each topic before creating it.

Next: [JSON Schema](../reference/json-schema.md)
