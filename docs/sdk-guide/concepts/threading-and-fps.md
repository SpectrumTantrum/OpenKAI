# Threading And FPS

This page explains how OpenKAI modules run continuously. Many modules own a `_Thread`, read a target `FPS` from JSON, and repeat an `update()` loop while the application is alive.

## Start With The Thread Config

In `helloOK.json`, both `view` and `cam` define a nested thread object:

```json
"thread": {
  "name": "thread",
  "class": "_Thread",
  "FPS": 30
}
```

Source: `jsonCfg/helloOK.json:23` and `jsonCfg/helloOK.json:36`.

`FPS` means frames per second. In OpenKAI, it is the target rate for a module's repeated work loop.

## `_Thread`

`_Thread` stores the run state, target FPS, measured FPS, timing values, and the pthread handle used to run a module loop.

`_Thread::init()` reads `FPS` from JSON and stores it as the target rate at `src/Base/_Thread.cpp:48`.

`_Thread::startThread()` starts the pthread at `src/Base/_Thread.cpp:77`.

`_Thread::run()`, `pause()`, and `stop()` update the desired thread state at `src/Base/_Thread.cpp:107`.

## How `_ModuleBase` Uses `_Thread`

Threaded modules usually inherit from `_ModuleBase`.

During initialization, `_ModuleBase` creates a `_Thread` from the nested `thread` config at `src/Base/_ModuleBase.cpp:25`.

During startup, `_ModuleBase::start()` starts the thread with the module's `update()` function at `src/Base/_ModuleBase.cpp:71`.

This gives each module its own control loop. A **control loop** is a repeated cycle that reads state, performs work, and waits until the next target tick.

## `autoFPS()`

`autoFPS()` is the timing helper that tries to keep a module near its target FPS.

It measures elapsed time, computes the remaining sleep time for the current frame, and calls `sleepT()` when the loop is ahead of schedule. See `src/Base/_Thread.cpp:181`.

`sleepT()` uses a condition variable with either timed wait or indefinite wait behavior. See `src/Base/_Thread.cpp:146`.

This is not a hard real-time scheduler. If a module's work takes longer than the target frame time, `autoFPS()` cannot make it run faster.

## The Update Loop Pattern

`_Camera::update()` is a clear example:

```text
while thread is alive:
  open camera if needed
  autoFPS()
  read frame
  copy latest frame
```

The implementation starts at `src/Vision/_Camera.cpp:80`. It calls `autoFPS()` at `src/Vision/_Camera.cpp:93` and copies the latest frame at `src/Vision/_Camera.cpp:98`.

`_Resize::update()` follows the same shape: loop while alive, call `autoFPS()`, then run `filter()`. See `src/Vision/ImgFilter/_Resize.cpp:48`.

## Waking Other Threads

`_Thread` also supports `vRunThread`, a JSON array of module names whose threads can be woken by this thread. `_Thread::link()` reads `vRunThread` at `src/Base/_Thread.cpp:59`, and `runAll()` signals those linked threads at `src/Base/_Thread.cpp:140`.

This is a framework-specific coordination hook. You do not need it for simple fixed-rate modules.

## What Goes Wrong

If `FPS` is set too high, the module loop may run as fast as it can but still miss the target. The config says the desired rate, not a guaranteed rate.

If `thread` is missing, `_ModuleBase` cannot create the `_Thread`, so `start()` has nothing to run.

If a module blocks inside its own work, its own loop falls behind. Other modules have their own threads, but shared data or device access can still make poor blocking behavior visible elsewhere.

Next: [Lifecycle](lifecycle.md)
