# Configuration

This page explains the JSON structure OpenKAI uses to define an app. A config file names module instances, selects their C++ classes, enables or disables them, configures worker threads, and provides module-specific fields.

## Start With `helloOK.json`

```json
{
  "APP": {
    "class": "ModuleMgr",
    "appName": "helloOK",
    "bLog": true,
    "bStdErr": true
  },
  "view": {
    "class": "_WindowCV",
    "bON": true,
    "thread": { "name": "thread", "class": "_Thread", "FPS": 30 },
    "vBASE": ["cam"]
  },
  "cam": {
    "class": "_Camera",
    "bON": true,
    "thread": { "name": "thread", "class": "_Thread", "FPS": 30 },
    "deviceID": 0
  }
}
```

Source: `jsonCfg/helloOK.json:1`.

An OpenKAI config is a top-level JSON object. `APP` contains app-level settings. Other top-level keys usually define module instances.

## Top-Level Keys Are Module Names

In `helloOK.json`, `view` and `cam` are module names. `ModuleMgr::createAll()` reads the top-level key into a name variable at `src/Module/ModuleMgr.cpp:67`.

The configured name is important because other modules use it later when they link by name. See [Linking](linking.md).

If two top-level entries use the same name after includes are processed, `createAll()` skips duplicates; the duplicate check is at `src/Module/ModuleMgr.cpp:74`.

## Common Fields

`class` selects the C++ class to instantiate. `createAll()` reads it at `src/Module/ModuleMgr.cpp:80`.

`bON` controls whether the module is created. It defaults to true in `createAll()`, then the JSON value can turn it off. See `src/Module/ModuleMgr.cpp:89`.

`thread` configures the module's worker thread for `_ModuleBase`-derived modules. `_ModuleBase` reads it at `src/Base/_ModuleBase.cpp:25`.

`FPS` inside `thread` sets the target loop rate. `_Thread::init()` reads it at `src/Base/_Thread.cpp:48`.

`bLog` and `fConfig` are common base fields read by `BASE::init()` at `src/Base/BASE.cpp:30`.

## Module-Specific Fields

Modules can define their own JSON fields.

For `_Camera`, `deviceID` selects the camera device, and `vSizeRGB` asks for a frame size. `_Camera::init()` reads `deviceID` at `src/Vision/_Camera.cpp:30`, while `_Camera::open()` applies width and height at `src/Vision/_Camera.cpp:49`.

For `_WindowCV`, `bFullScreen` controls the display mode. `_WindowCV::init()` reads it at `src/UI/_WindowCV.cpp:28`.

The common rule is: base classes read common fields, and concrete modules read fields specific to their behavior.

## Includes

`APP.vInclude` lets one config load additional JSON files. `ModuleMgr::parseJsonFile()` reads `vInclude` at `src/Module/ModuleMgr.cpp:36`.

This lets large apps split configuration across files. The module manager stores the main config first, then appends included configs.

If an included file cannot be parsed, the loop continues because the include parse uses `IF_CONT`; that path is visible at `src/Module/ModuleMgr.cpp:40`.

## Comments In JSON Files

OpenKAI removes C-style block comments before parsing. `JsonCfg::parseJsonStr()` calls `delComment()` at `src/Module/JsonCfg.cpp:53`.

That means configs can contain `/* ... */` comments even though standard JSON does not allow comments.

Do not rely on line comments unless you have verified parser support. The static code path shown here only removes block comments.

## What Goes Wrong

If `class` is missing, `createAll()` skips that top-level entry because it cannot choose a C++ class. See `src/Module/ModuleMgr.cpp:83`.

If `bON` is false, the module does not exist at runtime. That is intentional for disabled modules, but it can surprise you when another module still tries to link to it.

If `thread` is malformed on a threaded module, `_ModuleBase` cannot create the `_Thread` object needed by `start()`.

Next: [Linking](linking.md)
