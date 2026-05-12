# OpenKAI SDK Guide Plan

## Collision Check

Existing `docs/` contents before this plan was created:

- `docs/misc.md`
- `docs/nginx.md`
- `docs/optimize.md`
- `docs/rtklib.md`
- `docs/setup.md`
- `docs/unicore.md`

No existing `docs/sdk-guide/` directory was present, so the planned files below do not collide with any existing repository files.

## Files To Create

### Root

- `docs/sdk-guide/index.md` - Introduce what OpenKAI is, what kinds of projects it supports, how to read this guide, and what prerequisites the reader should have.
- `docs/sdk-guide/_questions.md` - Collect source-code ambiguities or suspected documentation questions encountered during static reading, without changing source files.

### Getting Started

- `docs/sdk-guide/getting-started/installation.md` - Explain the Linux-oriented build flow, required dependencies at a high level, and the meaning of core CMake flags such as `WITH_ROS` and `USE_ROS`.
- `docs/sdk-guide/getting-started/your-first-run.md` - Walk through running `helloOK.json` and narrate what happens at each OpenKAI lifecycle stage.
- `docs/sdk-guide/getting-started/anatomy-of-helloOK.md` - Explain `helloOK.json` line by line, mapping JSON keys to module instances, thread settings, and links.

### Concepts

- `docs/sdk-guide/concepts/architecture-overview.md` - Present the core mental model: one executable, a JSON-defined module graph, and per-module worker threads.
- `docs/sdk-guide/concepts/modules.md` - Define what an OpenKAI module is, what `_ModuleBase` provides, and what a module typically owns.
- `docs/sdk-guide/concepts/the-factory.md` - Explain how a JSON `"class"` value becomes a real C++ object through `Module::createInstance()` and `ADD_MODULE`.
- `docs/sdk-guide/concepts/configuration.md` - Explain the common JSON shape and fields such as `class`, `name`, `bON`, `thread`, and `FPS`.
- `docs/sdk-guide/concepts/linking.md` - Explain linking by name, `findModule()`, the `vBASE` pattern, and direct pointer relationships between modules.
- `docs/sdk-guide/concepts/threading-and-fps.md` - Explain `_Thread`, `autoFPS()`, and the FPS-regulated `update()` loop pattern used by threaded modules.
- `docs/sdk-guide/concepts/lifecycle.md` - Explain `parseJsonFile -> createAll -> initAll -> linkAll -> startAll` and why the order matters.

### How To

- `docs/sdk-guide/how-to/trace-a-config-end-to-end.md` - Guide the reader through tracing `helloOK.json` from config to `_Camera` capture to `_WindowCV` display.
- `docs/sdk-guide/how-to/add-a-module-to-a-config.md` - Show how to add a new module instance to an existing config without writing new C++ code.
- `docs/sdk-guide/how-to/write-a-new-module.md` - Show the static steps for adding a new C++ module: header, implementation, CMake inclusion, and factory registration.
- `docs/sdk-guide/how-to/link-modules-by-name.md` - Provide practical recipes for linking modules using fields such as `vBASE` and module-specific reference keys.
- `docs/sdk-guide/how-to/enable-ros2-bridge.md` - Explain how to enable the optional ROS2 bridge flags and what the `_ROS_*` modules do inside OpenKAI.

### Reference

- `docs/sdk-guide/reference/json-schema.md` - List common JSON fields used by OpenKAI configs and explain what each field controls.
- `docs/sdk-guide/reference/module-catalog.md` - Catalog module classes discovered under `src/`, grouped by domain, with detailed entries for `_Camera`, `_Resize`, `_WindowCV`, and one ROS bridge module.
- `docs/sdk-guide/reference/cmake-flags.md` - List build flags discovered from `CMakeLists.txt` and summarize what each flag enables.

### Explanation

- `docs/sdk-guide/explanation/why-modules-and-json.md` - Explain the design philosophy and tradeoffs of an in-process, JSON-configured module graph.
- `docs/sdk-guide/explanation/when-openkai-fits.md` - Explain the kinds of robotics and unmanned-vehicle projects OpenKAI appears well-suited for, and where it may be a poor fit.
