# Troubleshooting: realtime WebSocket features silently stop working

**Symptom:** an app built on OpenKAI's WebSocket transport (e.g. LCAS's browser viewer)
loads fine, renders its static UI/geometry, but never receives live pushed updates —
no error, no crash, the connection just never delivers data (or never opens at all).

This doc covers the OpenKAI-side causes. For the LCAS-specific version of this incident
(viewer grid renders but never updates live), see
[`LCAS/docs/troubleshoot-viewer-realtime.md`](../../LCAS/docs/troubleshoot-viewer-realtime.md)
in the LCAS repo.

## The realtime chain (verified in code)

OpenKAI supplies the entire realtime push transport; consumer apps do not implement
their own WebSocket server.

```
consumer module (e.g. LCAS's _LCASctrl)
  -> _JSONbase::sendJson()        (src/Protocol/_JSONbase.cpp)
  -> m_pIO->write(...)            (m_pIO is a _IObase*, held by _ProtocolBase)
  -> _WebSocketServer::write()    (src/IO/_WebSocketServer.cpp, "class _WebSocketServer : public _IObase")
  -> browser WebSocket client (e.g. wsUIbase.js: `new WebSocket('ws://<host>:7890')`)
```

- `_ProtocolBase` (`src/Protocol/_ProtocolBase.h`) holds the generic `_IObase *m_pIO;`
  pointer. Which concrete `_IObase` subclass it binds to is resolved at config-link time
  from whatever module name the consumer's JSON config points `"_IObase"` at (e.g.
  `"_IObase": "wsServer"`).
- `_WebSocketServer` (`src/IO/_WebSocketServer.h/.cpp`) is one concrete `_IObase`
  implementation — a WebSocket server bound to a config-specified port (commonly 7890).
- `_JSONbase::sendJson()` (`src/Protocol/_JSONbase.cpp`) is the generic "send a JSON
  message over whatever `_IObase` is linked" primitive; any module built on
  `_JSONbase`/`_ProtocolBase` gets realtime push for free, with zero WebSocket code of
  its own, as long as the feature is actually compiled in (see below) and its config
  links `_IObase` to a `_WebSocketServer` instance.

## Compile-time gate: `USE_WSSERVER`

`_WebSocketServer.cpp`/`_WebSocket.cpp` are **only** added to the library build inside
this guard in `CMakeLists.txt`:

```cmake
if(WITH_IO)
  ...
  if(USE_WSSERVER)
    add_definitions(-DUSE_WSSERVER)
    set(OpenKAI_cpp ${OpenKAI_cpp} src/IO/_WebSocket.cpp)
    set(OpenKAI_cpp ${OpenKAI_cpp} src/IO/_WebSocketServer.cpp)
    set(OpenKAI_lib ${OpenKAI_lib} ws)
  endif()
endif()
```

If `WITH_IO` or `USE_WSSERVER` is `OFF` at configure time, `_WebSocketServer` is not
even compiled into `libOpenKAI`. A consumer app that links `"_IObase": "wsServer"`
against such a build will still start and run — `_JSONbase`'s `m_pIO` machinery doesn't
hard-fail on a missing feature the way a linker would — but nothing ever arrives on the
browser side, because the server that would have accepted the connection and pushed
data was never built.

**As of this branch, this project's own `CMakeLists.txt` defaults `USE_WSSERVER`,
`USE_OPEN3D`, `USE_SCEPTER_SDK`, `WITH_3D`, and `WITH_DEFAULT_MODULES` to `OFF`.**
A plain `cmake -S . -B build && cmake --build build` with no explicit `-D` flags
produces a `libOpenKAI` with the WebSocket server (and 3D viewer support, and the
camera driver) compiled out entirely. Anyone consuming this library for a realtime-UI
app must pass the relevant flags explicitly — see "The fix" below.

## Root causes of "loads but doesn't update" (a real incident, generalized)

This has been observed in practice, more than once, in slightly different shapes.
None of them crash or log an error — that's what makes them nasty.

**A. Feature flags off at build time.** Covered above: `USE_WSSERVER=OFF` (or any of
its prerequisite `WITH_*`/`USE_*` flags) silently omits the server from the built
library. The UI is static HTML/JS — it renders fine on its own, so nothing *looks*
broken until you notice nothing ever moves.

**B. Stale binary vs. moved/updated library.** An app binary keeps running against
whatever `libOpenKAI.so` it was originally linked/loaded against, while a *different*
build of the library (different feature flags, different upstream dependency ABI) gets
installed underneath it. Symbols still resolve at load time — dynamic linking is
version-number-based, not content-based — so the process starts and runs. Features that
depend on the *behavior* of the newer/older code (not just its symbol names) silently
degrade instead of failing loudly. This exact class of bug has hit this deployment
before in a different subsystem (a depth-camera filter became a silent no-op after a
library/binary generation mismatch went unnoticed) — the underlying failure mode is the
same one described here for the WebSocket path.

**C. The empty `CMAKE_BUILD_TYPE` cache gotcha.** `project()` pre-registers an *empty*
`CMAKE_BUILD_TYPE` cache entry as a side effect of being called. A later, non-`FORCE`
`set(CMAKE_BUILD_TYPE "Release" CACHE STRING "")` in the same `CMakeLists.txt` **does
not override an already-registered cache entry**, even an empty one — so a bare
`cmake -S . -B build` run can silently produce a build with neither `Debug` nor
`Release` optimization flags applied, regardless of what the `CMakeLists.txt` author
intended as the default. This is a generic CMake behavior (not specific to this
project) and has been directly observed in this deployment's own `CMakeCache.txt`.
It's a distinct failure mode from A/B — it doesn't remove a feature, but it can produce
an inconsistent build type between a library and the binary that links it, which
matters when fixed-size types (e.g. Eigen) have alignment that depends on optimization
flags (`-march=native` etc.) — a mismatch there aborts at runtime with an ABI assertion
rather than degrading silently, so it's easier to catch than A/B, but still worth ruling
out explicitly rather than assuming the cache reflects the source.

## The fix

Always pass the required flags **explicitly on the `cmake` command line**, not just as
`CMakeLists.txt` defaults — command-line `-D` values are set before `project()` runs and
reliably win over both the empty-cache gotcha (C) and any stale/backwards default in the
source:

```bash
cmake -S . -B build -G Ninja \
  -D CMAKE_BUILD_TYPE=Release \
  -D BUILD_AS_LIB=ON \
  -D USE_GLOG=ON -D USE_GUI=ON -D USE_OPEN3D=ON -D USE_OPENCV=ON \
  -D USE_SCEPTER_SDK=ON -D USE_WSSERVER=ON \
  -D WITH_3D=ON -D WITH_DEFAULT_MODULES=ON
cmake --build build -j"$(nproc)"
sudo cmake --build build --target install
sudo ldconfig
```

Then rebuild the consumer app (e.g. LCAS) against the freshly installed library, not
just the library alone — see the LCAS-side doc for why both layers need to move
together.

**On a dev machine, installing straight to `/usr/local` is often the wrong move** —
it clobbers the system-wide `libOpenKAI.so` while other uncommitted work may still
depend on it. `LCAS/docs/build-dev-machine.md` (in the LCAS repo) is the preventive
recipe for this: the full two-layer configure using a private install prefix
(`CMAKE_INSTALL_PREFIX` under `$HOME`, no `sudo` needed), worked through against a
real machine, with every flag below verified against this branch's actual
`CMakeLists.txt`.

## How to verify

- **Check what a binary is actually linked against — don't trust `ldd`.** `ldd` lists
  *transitive* dependencies (everything in the whole dependency graph), so it can look
  identical before and after a library swap even when the direct link target changed.
  Use `readelf -d <binary-or-lib> | grep NEEDED` to see only the direct, immediate
  link-time dependencies of that one file.
- **Check the WebSocket server is actually listening**, on the machine actually running
  the app: `ss -ltn | grep :7890` (or the configured port). If nothing is listening,
  the server was never started — check `USE_WSSERVER`/`WITH_IO` in the build that
  produced the running library, not just the source tree.
- **Rebuild both layers, not just the app.** If the library moved (new build, new
  install), the consumer binary needs to be relinked/rebuilt against it too — an old
  binary against a new library (or vice versa) is exactly failure mode B above.
