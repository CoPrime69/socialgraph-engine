#include "sg/common/lifecycle.hpp"

#include <csignal>
#include <pthread.h>
#include <stdexcept>
#include <unistd.h>

namespace sg::common {
namespace {

sigset_t shutdown_signals() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  return set;
}

}  // namespace

void block_shutdown_signals() {
  const sigset_t set = shutdown_signals();
  if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {
    throw std::runtime_error("pthread_sigmask failed");
  }
  std::signal(SIGPIPE, SIG_IGN);
}

int wait_for_shutdown_signal() {
  const sigset_t set = shutdown_signals();
  int signal = 0;
  if (sigwait(&set, &signal) != 0) {
    throw std::runtime_error("sigwait failed");
  }
  return signal;
}

void request_shutdown() { ::kill(::getpid(), SIGTERM); }

}  // namespace sg::common
