#pragma once

/// @file lifecycle.hpp
/// Process lifecycle: graceful shutdown on SIGINT/SIGTERM.

namespace sg::common {

/// Blocks SIGINT and SIGTERM in the calling thread (inherited by every thread spawned later) and
/// ignores SIGPIPE. Call first thing in main(), before any thread is created.
void block_shutdown_signals();

/// Waits for SIGINT or SIGTERM (see block_shutdown_signals()) and returns the signal number.
int wait_for_shutdown_signal();

/// Sends SIGTERM to this process, waking wait_for_shutdown_signal() (e.g. after a fatal error in a
/// background thread).
void request_shutdown();

}  // namespace sg::common
