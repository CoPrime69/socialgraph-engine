# ADR-0002: C++20 services on Drogon

## Context
The project must keep a modern C++ backend. Requests are small JSON calls; the expensive part is
graph computation, which is done once per graph version and cached (ADR-0006). Every public request
reaches a service through the nginx gateway (ADR-0008), which keeps a pool of keep-alive
connections open to each upstream.

The services first ran on cpp-httplib 0.14, a blocking server with a fixed worker pool. A worker
thread serves one connection for as long as that connection stays open, so every idle keep-alive
connection from the gateway pins a pool thread. With more gateway connections than pool threads,
requests waited for a thread to free up, and load tests showed bimodal latency with a tail of up
to seconds.

## Decision
* **C++20** (GCC 13 / Clang 18): `std::span`, `std::jthread`/`stop_token`, `std::atomic<std::shared_ptr>`,
  ranges, designated initialisers. C++23 features are not required.
* **Drogon 1.8.7** (Ubuntu 24.04 package `libdrogon-dev`) serves HTTP. It runs one epoll event loop
  per I/O thread (`HTTP_THREADS`, default one per CPU core) and multiplexes all connections over
  them: an idle keep-alive connection costs a file descriptor, not a thread.
* Only Drogon's HTTP server is used. `sg::common::HttpServer` installs one pre-routing advice that
  answers every request itself with its own router (literal segments and `:param` segments, first
  registered match wins), so Drogon's controllers, ORM, sessions, static files and compression stay
  unused. Handlers see `sg::common::Request` / `Response`, never Drogon types. The wrapper adds
  request ids, structured access logs, JSON error mapping, body limits, health/readiness endpoints
  and graceful shutdown; handlers never see raw exceptions.
* Handlers that block (PostgreSQL, Redis, Argon2id) run on a per-server worker pool
  (`HTTP_WORKER_THREADS`, default 32) and hand the response back to the event loop. analytics-service
  handlers only read immutable in-memory snapshots and run inline on the event loops; its requests
  with `?min_version`, which may wait for a newer snapshot, still go to the pool.
* cpp-httplib's `Client` stays for the few service-to-service calls and the `healthcheck` subcommands.
* **nlohmann/json**, **libsodium**, **libpqxx**, **hiredis** – all from distro packages, so builds are
  reproducible offline from the Ubuntu 24.04 package set (Dockerfile and CI install the same list).

## Measurements
A rough spike on a shared 4-vCPU VM, so indicative only: a hello-world JSON route under autocannon
with 64 connections answered about 39k requests/s on cpp-httplib (8-thread pool) and about 155k
requests/s on Drogon (4 I/O threads). This is not a benchmark of the services, whose handlers do
real work.

## Alternatives
* **cpp-httplib** (the previous choice) – a small, simple blocking server. Replaced because of the
  thread-per-connection behaviour described above; raising the pool size only moves the limit and
  costs a thread stack per idle connection.
* **Boost.Beast/Asio** – maximal control and async I/O, but several hundred lines of protocol plumbing
  (parsing limits, keep-alive, pipelining, timeouts) before the first route.
* **gRPC between services** – there is only one synchronous internal call (snapshot), so the cost of
  protobuf toolchains isn't justified; events carry the rest.

## Consequences
* `drogon::app()` is a process-wide singleton that cannot be restarted, so one engine serves every
  `HttpServer` of a process: it starts with the first `listen()`, its listeners are fixed from then
  on, and it dispatches each request by listener port to the server that owns it. Production runs
  one server per process and quits the engine when that server stops. Tests that create servers
  one after another get ephemeral ports from a few spare listeners created at start-up.
* Code on an event loop must never block. Blocking handlers are the default; running inline is an
  explicit per-service choice.
* The engine's own limits are process-wide: request bodies above `max(max_body_bytes, 1 MiB)` are
  refused by Drogon with a bare 413 before reaching the server (smaller excess gets the JSON 413).
* The distribution's `DrogonConfig.cmake` requires the development packages of every database
  backend Drogon was built with, so CMake locates `libdrogon`, trantor and jsoncpp directly.
* Swapping the transport later still only touches `libs/common` (`http.hpp`/`http.cpp`).
