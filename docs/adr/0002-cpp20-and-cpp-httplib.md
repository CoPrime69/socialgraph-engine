# ADR-0002: C++20 services on cpp-httplib

## Context
The project must keep a modern C++ backend. Requests are small JSON calls; the expensive part is
graph computation, which is done once per graph version and cached (ADR-0006). Expected concurrency
is moderate (tens to low hundreds of concurrent clients per instance).

## Decision
* **C++20** (GCC 13 / Clang 18): `std::span`, `std::jthread`/`stop_token`, `std::atomic<std::shared_ptr>`,
  ranges, designated initialisers. C++23 features are not required.
* **cpp-httplib 0.14** (Ubuntu 24.04 package) for HTTP: a blocking server with a fixed worker pool.
  It is wrapped by `sg::common::HttpServer`, which adds request ids, structured access logs, JSON error
  mapping, body limits, health/readiness endpoints and graceful shutdown; handlers never see raw
  exceptions.
* **nlohmann/json**, **libsodium**, **libpqxx**, **hiredis** – all from distro packages, so builds are
  reproducible offline from the Ubuntu 24.04 package set (Dockerfile and CI install the same list).

## Alternatives
* **Drogon** – fast, async, built-in ORM and PG client. Rejected: large framework surface, its own
  build/dependency chain, and its async model buys little when the heavy work is CPU-bound and cached.
* **Boost.Beast/Asio** – maximal control and async I/O, but several hundred lines of protocol plumbing
  per service before the first route; a thread-pool model is sufficient for this workload.
* **gRPC between services** – there is only one synchronous internal call (snapshot), so the cost of
  protobuf toolchains isn't justified; events carry the rest.

## Consequences
* Each in-flight request holds a worker thread; slow clients are bounded by read/write timeouts.
  The load-test results in `docs/performance.md` show where this limits throughput.
* Swapping the transport later only touches `libs/common/http` and each service's `http/` layer.
