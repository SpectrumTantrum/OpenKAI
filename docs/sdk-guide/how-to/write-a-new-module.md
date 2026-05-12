# Write A New Module

This guide explains the static steps required to make a new C++ module available from JSON. The pattern is: create a class, compile its source, include its header, register it in the factory, then instantiate it from config.

## Start From The Smallest Shape

A typical threaded module derives from `_ModuleBase`:

```cpp
class _YourThing : public _ModuleBase
{
public:
  virtual bool init(const json& j);
  virtual bool link(const json& j, ModuleMgr* pM);
  virtual bool start(void);

private:
  void update(void);
};
```

`_ModuleBase` provides `init`, `link`, `start`, state checks, pause/resume/stop, and access to the protected `_Thread *m_pT`. The relevant interface is declared at `src/Base/_ModuleBase.h:27`.

## Step 1: Choose A Domain Directory

Place the new module under the domain it belongs to, such as `src/Vision/`, `src/UI/`, `src/IO/`, or another existing subtree.

The domain matters because CMake and the factory are organized by feature gates. For example, vision modules are registered under `WITH_VISION` and often `USE_OPENCV`; `_Camera` and `_Resize` are registered in that area at `src/Module/Module.cpp:272`.

## Step 2: Implement `init()`

Use `init()` to read JSON configuration.

For a threaded module, call the parent class first so the common fields and nested thread are initialized:

```cpp
IF_F(!_ModuleBase::init(j));
```

`_ModuleBase::init()` creates the `_Thread` from the `thread` object at `src/Base/_ModuleBase.cpp:25`.

If your module inherits from a more specific base, follow that base's pattern. `_Camera::init()` calls `_VisionBase::init()` first, then reads camera-specific fields at `src/Vision/_Camera.cpp:28`.

## Step 3: Implement `link()`

Use `link()` for references to other modules. Do not resolve dependencies in `init()`, because other modules may not be ready yet.

The usual pattern is:

```cpp
string n = "";
jKv(j, "SomeDependency", n);
m_pDependency = (SomeType *)(pM->findModule(n));
NULL_F(m_pDependency);
```

`_Resize::link()` is a compact example: it reads `_VisionBase`, calls `findModule()`, casts the result, and fails if the pointer is missing. See `src/Vision/ImgFilter/_Resize.cpp:30`.

## Step 4: Implement `start()` And `update()`

If your module owns a normal worker loop, `start()` should start the thread with your `update()` function.

`_Camera::start()` does this at `src/Vision/_Camera.cpp:74`.

Inside `update()`, loop while the thread is alive, call `autoFPS()`, and do one unit of module work. `_Resize::update()` shows the minimal pattern at `src/Vision/ImgFilter/_Resize.cpp:48`.

If your module does not need a worker thread, derive directly from `BASE` or follow a local non-threaded pattern instead.

## Step 5: Add The Source To CMake

The new `.cpp` file must be compiled into the target.

OpenKAI's CMake file groups sources by feature gate. For example, `WITH_ROS` collects `src/ROS/*.cpp` files at `CMakeLists.txt:348`, while vision modules are listed under the vision/OpenCV block that registers `_Camera` and `_Resize` in the factory.

Choose the existing block that matches your module's domain and dependencies.

## Step 6: Include The Header In `Module.h`

The factory can only instantiate types visible to the compiler.

`Module.h` includes module headers under matching gates. For example, the ROS bridge header is included when `WITH_ROS` is enabled at `src/Module/Module.h:198`.

Add your header under the same condition that compiles your source.

## Step 7: Register With `ADD_MODULE`

Register the class in `Module::createInstance()`:

```cpp
ADD_MODULE(_YourThing);
```

The `ADD_MODULE` macro is defined at `src/Module/Module.h:309`. Existing registrations live in `src/Module/Module.cpp`; `_Camera` is registered at `src/Module/Module.cpp:273`.

The registration must be under the same feature gates as the header and source.

## Step 8: Instantiate From JSON

Once the class is compiled and registered, a config can create it:

```json
"yourThing": {
  "class": "_YourThing",
  "bON": true,
  "thread": { "name": "thread", "class": "_Thread", "FPS": 30 }
}
```

`ModuleMgr::createAll()` reads that top-level key and calls the factory at `src/Module/ModuleMgr.cpp:54`.

## What Goes Wrong

If the source is not in CMake, the class will not be compiled.

If the header is not included in `Module.h`, the factory cannot name the type.

If `ADD_MODULE(_YourThing)` is missing, the JSON class name will never match a factory entry.

If your `link()` resolves dependencies too early or accepts null required dependencies, the module may start without the objects it needs.

Next: [Link Modules By Name](link-modules-by-name.md)
