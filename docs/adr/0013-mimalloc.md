# ADR-0013: mimalloc as the services' allocator

## Context
Request handling allocates many short-lived objects on many threads: request and header strings,
JSON trees, result rows, response bodies. `perf` on the mixed load test showed glibc's `malloc`,
`_int_malloc` and `malloc_consolidate` among the hottest functions of the worker threads.

## Decision
The four service executables link mimalloc 2.1 (Ubuntu package `libmimalloc-dev` /
`libmimalloc2.0`), which replaces `malloc`/`free` process-wide by symbol interposition. The choice
lives in [`cmake/Allocator.cmake`](../../cmake/Allocator.cmake): `SG_ALLOCATOR=auto` (default) uses
mimalloc when it is installed, `system` keeps glibc. Sanitizer builds always keep the system
allocator, since ASan and TSan provide their own.

## Measurements
Same build, fresh stack, two 25 s runs of the mixed workload after a warm-up run
([performance.md](../performance.md)):

| Allocator | Mean of two runs |
|---|---:|
| glibc malloc | 5,702 req/s |
| jemalloc 5.3 | 6,084 req/s |
| mimalloc 2.1 | 6,429 req/s |

## Alternatives
* **jemalloc** – mature, good introspection; measured slower than mimalloc here.
* **tcmalloc** – not packaged for this toolchain without building gperftools or Abseil.
* **Custom arenas per request** – would need allocator-aware JSON and string types through the
  whole stack.

## Consequences
* (+) About 13 % more throughput than glibc on the mixed workload, with no code changes.
* (−) One more runtime dependency in the service images.
* (−) Memory is returned to the OS on mimalloc's schedule, so resident memory can look higher than
  with glibc after a burst.
