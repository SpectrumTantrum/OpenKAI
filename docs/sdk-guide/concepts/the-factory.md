# The Factory

This page explains how a JSON class name such as `_Camera` becomes a real C++ object. OpenKAI uses a factory: a small piece of code that maps strings from configuration to compiled C++ classes.

## Start With `"class": "_Camera"`

In `helloOK.json`, the camera instance says:

```json
"cam": {
  "class": "_Camera",
  "bON": true
}
```

Source: `jsonCfg/helloOK.json:33`.

`"class": "_Camera"` does not load code dynamically from the JSON file. It selects one of the classes that was already compiled into the `OpenKAI` binary.

## Where Creation Happens

During `createAll()`, `ModuleMgr` reads the class string and calls `Module::createInstance()`:

```text
read "class"
skip if disabled
createInstance(className)
store object under JSON key
```

The implementation reads `class` at `src/Module/ModuleMgr.cpp:80`, checks `bON` at `src/Module/ModuleMgr.cpp:89`, calls `createInstance()` at `src/Module/ModuleMgr.cpp:97`, and stores the module name at `src/Module/ModuleMgr.cpp:104`.

A **factory registration** is the code entry that tells the factory which C++ class to construct for a given class name string.

## The `ADD_MODULE` Macro

The factory is a long list of `ADD_MODULE(...)` calls in `Module::createInstance()`. The macro is defined in `src/Module/Module.h:309`.

The macro compares the requested class name with the C++ class name and returns a newly created instance when they match.

For example, `_Camera` is registered under the `WITH_VISION` and `USE_OPENCV` gates at `src/Module/Module.cpp:272`.

`_WindowCV` is registered under `WITH_UI` and `USE_OPENCV` at `src/Module/Module.cpp:245`.

## Includes Must Match Registrations

The factory can only construct classes that are visible to the compiler. `Module.h` includes module headers under the same feature gates used by the factory. For example, the ROS bridge header is included when `WITH_ROS` is set at `src/Module/Module.h:198`.

This means a module class needs three things to be usable from JSON:

- Its source file must be included in the build.
- Its header must be included by `Module.h` under the correct gate.
- Its class must be registered in `Module.cpp` with `ADD_MODULE(...)`.

If any of those are missing, the JSON `class` value may look correct but still fail at runtime creation.

## Why Build Flags Matter

The factory list is compiled conditionally. A module class can be present in the source tree but absent from the current binary.

For example, `_Camera` is behind vision and OpenCV gates. If the build does not enable the relevant flags, `ADD_MODULE(_Camera)` is not compiled into `createInstance()`.

This is why [Installation](../getting-started/installation.md) recommends `WITH_DEFAULT_MODULES` for the first camera/window run: it turns on the common module families and OpenCV support needed by `helloOK.json`.

## What Goes Wrong

If `"class"` is misspelled, `createInstance()` eventually returns `nullptr`. `ModuleMgr::createAll()` logs that the instance was not created and continues; that branch is at `src/Module/ModuleMgr.cpp:97`.

If the class name is spelled correctly but the module was not compiled in, the result is the same from the JSON user's point of view: no module instance is created.

If another module links to the missing instance, `findModule()` cannot return it during the later `linkAll()` stage.

Next: [Configuration](configuration.md)
