# Platform support

FragmentX keeps its allocator, workload model, optimizer, and `.fxtrace` event
model in shared C++ code. Capturing an arbitrary process uses the operating
system's dynamic loader, so that part has a separate backend for each OS.

| Platform | Process capture backend | Current limits |
| --- | --- | --- |
| Linux | `LD_PRELOAD` shared library | Static binaries and secure-execution programs can reject loader injection. |
| macOS | `DYLD_INSERT_LIBRARIES` interposition library | SIP and hardened runtime can block injection for protected programs. |
| Windows | No process injection backend yet | Applications can use the instrumented `CaptureSession` API. |

Build on Linux or macOS with process capture enabled (it is enabled by default):

```sh
cmake -S . -B build -DFRAGMENTX_INTERPOSITION=ON
cmake --build build
./build/fragmentx capture --output workload.fxtrace -- ./your_program
```

On Linux the backend uses `LD_PRELOAD`; on macOS it uses
`DYLD_INSERT_LIBRARIES`. Both backends write the same versioned trace records,
so analysis and optimization do not depend on the capture mechanism. The
instrumented API remains available on platforms without a process injection
backend.

The process backends currently record calls routed through the common C heap
functions (`malloc`, `calloc`, `realloc`, and `free`), including aligned
allocation entry points. Allocator-specific information such as the true
usable size is not available in this mode; traces therefore leave
`allocated_size` unset. Static binaries, custom allocators that bypass these
entry points, or protected processes can produce incomplete traces.
