#pragma once

namespace sg::common::detail {

/// Calls sodium_init() exactly once (thread-safe). @throws std::runtime_error on failure.
void ensure_sodium();

}  // namespace sg::common::detail
