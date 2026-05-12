# Linking

This page explains how OpenKAI modules connect to each other. OpenKAI links modules by name: one module stores another module's configured name in JSON, then resolves that name into a C++ pointer during the `linkAll()` stage.

## Start With `vBASE`

In `helloOK.json`, the window module refers to the camera module like this:

```json
"view": {
  "class": "_WindowCV",
  "vBASE": ["cam"]
}
```

Source: `jsonCfg/helloOK.json:20`.

`vBASE` is a common pattern for UI-like modules: it is an array of module names that the module should use as inputs or draw sources.

## Link After Create

OpenKAI does not resolve names while parsing JSON. It waits until after all modules have been created.

That order is visible in `main()`: `createAll()` runs before `linkAll()` at `src/main.cpp:63` and `src/main.cpp:69`.

This matters because `view` can only get a pointer to `cam` after `cam` exists.

## `findModule()`

`ModuleMgr::findModule()` is the central name lookup function. It scans the created modules and returns the one whose configured name matches the requested name. See `src/Module/ModuleMgr.cpp:229`.

`_UIbase::link()` shows the `vBASE` pattern directly:

```text
read vBASE
for each name:
  find module by name
  store pointer
```

The implementation reads `vBASE` at `src/UI/_UIbase.cpp:32` and calls `findModule()` at `src/UI/_UIbase.cpp:37`.

## Module-Specific Link Fields

Not every link uses `vBASE`. Some modules define a field whose name describes the expected dependency.

For example, `_Resize` reads `_VisionBase` and resolves it to a vision module pointer. See `src/Vision/ImgFilter/_Resize.cpp:34`.

A config for that pattern would look like:

```json
"small": {
  "class": "_Resize",
  "bON": true,
  "thread": { "name": "thread", "class": "_Thread", "FPS": 30 },
  "_VisionBase": "cam"
}
```

Here, `_VisionBase` means "the input vision module for this resize module." The value `"cam"` is still a configured module name.

## Links Are Direct Pointers

After linking, modules usually hold direct C++ pointers to the modules they use.

This is why links are fast and simple inside a single OpenKAI process. It is also why the configured names must be correct: a bad name does not point to an external channel or late-bound service; it simply fails to resolve to a module object.

## What Goes Wrong

If a name is misspelled in `vBASE`, `_UIbase` skips that entry because `findModule()` returns no module. The skip path is at `src/UI/_UIbase.cpp:37`.

If a required module-specific link is missing, the module can fail its `link()` method. `_Resize` requires its `_VisionBase` pointer and returns false if it is missing at `src/Vision/ImgFilter/_Resize.cpp:36`.

If a dependency is disabled with `bON: false`, it will not exist for linking even if its JSON entry is present.

Next: [Threading And FPS](threading-and-fps.md)
