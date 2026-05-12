# Your First Run

This page walks through what happens when OpenKAI starts with `jsonCfg/helloOK.json`. It does not assume you already know the framework vocabulary: the key idea is that OpenKAI reads a JSON app description, creates module objects, connects them, starts their threads, and then keeps the process alive.

## The Command

```bash
./OpenKAI jsonCfg/helloOK.json
```

`main()` expects one argument: the JSON file path. If no argument is provided, it prints `Usage: ./OpenKAI [JSON file]`; that check is implemented at `src/main.cpp:21`.

For this specific config, the binary needs the `_Camera` and `_WindowCV` classes compiled in. A practical build choice is the common module set described in [Installation](installation.md).

## The Config It Reads

`helloOK.json` defines three top-level module entries after `APP`:

```json
"console": { "class": "_Console", "bON": false },
"view": { "class": "_WindowCV", "bON": true, "vBASE": ["cam"] },
"cam": { "class": "_Camera", "bON": true }
```

Source: `jsonCfg/helloOK.json:8`, `jsonCfg/helloOK.json:20`, and `jsonCfg/helloOK.json:33`.

`console` is disabled with `bON: false`, so it is listed in the file but skipped during module creation. `view` and `cam` are enabled.

## Stage 1: Parse The JSON

The first runtime stage is `parseJsonFile`, called from `main()` at `src/main.cpp:52`.

Parsing reads the JSON into a `JsonCfg` object. `JsonCfg` reads the file contents and parses the string at `src/Module/JsonCfg.cpp:24` and `src/Module/JsonCfg.cpp:53`.

The module manager also supports `APP.vInclude`, a list of extra JSON files to load. That include pass is implemented at `src/Module/ModuleMgr.cpp:33`.

If parsing fails, `main()` prints a parse failure and exits before any module instances are created. At this point, no device has been opened and no worker thread has started.

## Stage 2: Create Module Objects

After parsing, `main()` calls `createAll()` at `src/main.cpp:63`.

`createAll()` walks every top-level JSON object, reads the key as the module name, reads the nested `class`, skips modules with `bON` set to `0`, and asks the factory to create a C++ object. The relevant implementation starts at `src/Module/ModuleMgr.cpp:54`.

For `helloOK.json`, this means:

- `console` is skipped because `bON` is `false`.
- `view` becomes an instance of `_WindowCV`.
- `cam` becomes an instance of `_Camera`.

If `_WindowCV` or `_Camera` was not compiled into the binary, creation fails for that entry. The failure is local to that module, but later links that depend on it will not have a target to find.

## Stage 3: Initialize Each Module

Next, `main()` calls `initAll()` at `src/main.cpp:66`.

Initialization gives each module its own JSON object. The base class reads common fields such as `name`, `class`, `bLog`, and `fConfig` at `src/Base/BASE.cpp:26`.

Threaded modules inherit from `_ModuleBase`. During initialization, `_ModuleBase` creates a `_Thread` from the nested `thread` object at `src/Base/_ModuleBase.cpp:25`.

For `cam`, `_Camera::init()` reads camera-specific fields such as `deviceID`, `nInitRead`, and `bResetCam` at `src/Vision/_Camera.cpp:28`.

For `view`, `_WindowCV::init()` reads UI-specific fields such as `bFullScreen` and window size at `src/UI/_WindowCV.cpp:24`.

If a threaded module has no valid `thread` object, `_ModuleBase` cannot create the worker thread object it needs.

## Stage 4: Link Modules By Name

After initialization, `main()` calls `linkAll()` at `src/main.cpp:69`.

**Linking by name** means a module reads another module's configured name from JSON and asks `ModuleMgr` to return the matching C++ object pointer. The lookup function is `findModule()`, implemented at `src/Module/ModuleMgr.cpp:229`.

In `helloOK.json`, `view` uses:

```json
"vBASE": ["cam"]
```

Source: `jsonCfg/helloOK.json:29`.

`_UIbase` reads `vBASE`, calls `findModule()` for each name, and stores the resolved modules at `src/UI/_UIbase.cpp:28`.

If `vBASE` refers to a name that does not exist, the UI module will not receive that dependency. In this app, a misspelled camera name means the window has nothing useful to draw.

## Stage 5: Start Module Threads

Finally, `main()` calls `startAll()` at `src/main.cpp:72`.

For threaded modules, `start()` starts the module's update loop. `_ModuleBase::start()` delegates to `_Thread::startThread()` at `src/Base/_ModuleBase.cpp:71`.

`_Camera::start()` starts its own camera update loop at `src/Vision/_Camera.cpp:74`. Inside that loop, the camera opens the configured device, regulates its FPS, reads frames, and stores the latest frame. The frame read and copy happen at `src/Vision/_Camera.cpp:95`.

`_WindowCV::start()` starts the window update loop at `src/UI/_WindowCV.cpp:49`. The window loop regulates FPS, asks linked modules to draw into its frame, and displays it with OpenCV. See `src/UI/_WindowCV.cpp:55` and `src/UI/_WindowCV.cpp:65`.

## Stage 6: Keep The Process Alive

After `startAll()`, `main()` calls `waitForComplete()` at `src/main.cpp:75`.

The current implementation loops while `bComplete()` is false. `bComplete()` currently returns false, so the process is designed to keep running until it is interrupted. See `src/Module/ModuleMgr.cpp:206`.

The practical shutdown path is SIGINT. `main()` installs a signal handler at `src/main.cpp:44`; when SIGINT arrives, it calls `stopAll()`.

Next: [Anatomy Of helloOK](anatomy-of-helloOK.md)
