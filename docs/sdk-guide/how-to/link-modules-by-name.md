# Link Modules By Name

This guide shows practical linking patterns in OpenKAI configs. Linking by name means one module stores another module's configured name in JSON, then resolves it to a C++ pointer during `linkAll()`.

## Pattern 1: Use `vBASE` For UI Draw Sources

`helloOK.json` links the window to the camera like this:

```json
"view": {
  "class": "_WindowCV",
  "bON": true,
  "vBASE": ["cam"]
}
```

Source: `jsonCfg/helloOK.json:20`.

`_UIbase::link()` reads `vBASE` and stores pointers to the named modules. The implementation reads the array at `src/UI/_UIbase.cpp:32` and calls `findModule()` at `src/UI/_UIbase.cpp:37`.

Use this pattern when the module's base class expects a list of modules to draw or inspect.

## Pattern 2: Use A Module-Specific Dependency Field

Some modules use a field named after the expected dependency type.

For `_Resize`, the field is `_VisionBase`:

```json
"small": {
  "class": "_Resize",
  "bON": true,
  "thread": { "name": "thread", "class": "_Thread", "FPS": 30 },
  "_VisionBase": "cam",
  "vSizeRGB": [320, 240]
}
```

`_Resize::link()` reads `_VisionBase` at `src/Vision/ImgFilter/_Resize.cpp:34` and resolves the name with `findModule()`.

Use this pattern when the module implementation documents or reads a specific dependency key.

## Pattern 3: Link Chains, Not Just Pairs

You can link modules in a chain:

```json
"cam": {
  "class": "_Camera",
  "bON": true,
  "thread": { "name": "thread", "class": "_Thread", "FPS": 30 }
},
"small": {
  "class": "_Resize",
  "bON": true,
  "thread": { "name": "thread", "class": "_Thread", "FPS": 30 },
  "_VisionBase": "cam",
  "vSizeRGB": [320, 240]
},
"view": {
  "class": "_WindowCV",
  "bON": true,
  "thread": { "name": "thread", "class": "_Thread", "FPS": 30 },
  "vBASE": ["small"]
}
```

The graph is:

```mermaid
flowchart LR
  Cam["cam: _Camera"] --> Small["small: _Resize"]
  Small --> View["view: _WindowCV"]
```

During runtime, `_Resize` reads frames from `cam`, and `view` draws from `small`.

## Check Names Against Created Modules

Name lookup is exact. `ModuleMgr::findModule()` scans created modules and compares names at `src/Module/ModuleMgr.cpp:229`.

A module is only searchable if it was created. `createAll()` skips disabled modules when `bON` is `0`; that check is at `src/Module/ModuleMgr.cpp:89`.

So a link target must be:

- Present as a top-level config entry.
- Enabled with `bON: true` or no explicit false value.
- Created from a class compiled into the binary.
- Spelled exactly the same way in the dependency field.

## What Goes Wrong

If a link points to a disabled module, `findModule()` cannot return it.

If a link points to a class that failed factory creation, `findModule()` cannot return it.

If a module requires a dependency and the dependency is missing, `linkAll()` can fail before `startAll()` runs. `_Resize` is an example because it returns false when `_VisionBase` cannot be resolved at `src/Vision/ImgFilter/_Resize.cpp:36`.

Next: [Enable ROS2 Bridge](enable-ros2-bridge.md)
