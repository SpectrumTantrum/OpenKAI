# Modules

This page explains what an OpenKAI module is, what `_ModuleBase` adds on top of the base `BASE` class, and why most useful OpenKAI behavior lives inside module instances created from JSON.

## Start With A Module In Config

`helloOK.json` defines a camera module like this:

```json
"cam": {
  "class": "_Camera",
  "bON": true,
  "thread": { "name": "thread", "class": "_Thread", "FPS": 30 },
  "deviceID": 0,
  "vSizeRGB": [640, 480]
}
```

Source: `jsonCfg/helloOK.json:33`.

A **module** is a C++ object managed by OpenKAI and usually responsible for one device, transformation, protocol, data source, or UI surface. In this example, `cam` is the configured module name, and `_Camera` is the C++ class used to implement it.

## `BASE`: The Common Interface

All module-like objects share the `BASE` interface. `BASE::init()` reads common JSON fields such as `name`, `class`, `bLog`, and `fConfig` at `src/Base/BASE.cpp:26`.

The common lifecycle methods are:

- `init()` - read configuration.
- `link()` - connect to other modules after they exist.
- `start()` - begin work.
- `pause()`, `resume()`, and `stop()` - control runtime state.
- `draw()` and `console()` - optional output hooks used by UI and console modules.

If a module does not provide a usable `class`, `BASE::init()` fails because it checks for an empty class value at `src/Base/BASE.cpp:35`.

## `_ModuleBase`: A Threaded Module Base

Many domain modules inherit from `_ModuleBase`. `_ModuleBase` combines the basic `BASE` lifecycle with a `_Thread` object.

During `init()`, `_ModuleBase` reads the module's nested `thread` object and creates a `_Thread` at `src/Base/_ModuleBase.cpp:25`.

```json
"thread": {
  "name": "thread",
  "class": "_Thread",
  "FPS": 30
}
```

Source: `jsonCfg/helloOK.json:36`.

The `thread` object is not a separate top-level module. It is the per-module worker configuration used by `_ModuleBase`.

If this nested `thread` object is missing or not an object, `_ModuleBase::createThread()` cannot create the thread; the check is at `src/Base/_ModuleBase.cpp:36`.

## What A Module Owns

A module usually owns three things:

- Configuration values read during `init()`.
- Pointers to other modules resolved during `link()`.
- Runtime state updated in its `update()` loop.

For `_Camera`, configuration includes `deviceID`, `nInitRead`, and `bResetCam`, read at `src/Vision/_Camera.cpp:28`.

For `_Resize`, a linked dependency is the input vision module stored through `_VisionBase`; it reads the dependency name and resolves it at `src/Vision/ImgFilter/_Resize.cpp:30`.

For `_Camera`, runtime state includes the latest frame copied after a camera read at `src/Vision/_Camera.cpp:95`.

## The Update Loop

Most threaded modules override `update()`. `_ModuleBase` provides an empty default `update()` at `src/Base/_ModuleBase.cpp:84`, but concrete modules usually replace it with a loop.

`_Camera::update()` runs while the thread is alive, opens the camera if needed, calls `autoFPS()`, reads a frame, and copies it into the module's frame buffer. See `src/Vision/_Camera.cpp:80`.

The important pattern is:

```text
while the module thread is alive:
  regulate timing
  do one unit of module work
```

An **update loop** is the repeated work function for a threaded module. In OpenKAI, this is where continuous device polling, filtering, state updates, or display refresh usually happen.

## What Goes Wrong If A Module Is Misconfigured

If `class` names a C++ class that was not compiled into the binary, the factory cannot create the module. That means no object exists for `init()`, `link()`, or `start()`.

If `thread` is missing on a `_ModuleBase`-derived module, the module has no worker to start.

If a module-specific field is wrong, the module may still be created but fail later. For example, `_Camera` can be created with `deviceID: 0`, but `_Camera::open()` logs an error if the device cannot be opened; that path starts at `src/Vision/_Camera.cpp:42`.

Next: [The Factory](the-factory.md)
