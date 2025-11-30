#include "event_loop.h"

#include <iostream>
#include <csignal>

namespace EventBase {

EventLoop::EventLoop() {
  if ((event_base_ = ::event_base_new()) == nullptr) {
    std::cerr << "Failed to create an event_base";
  }
}

EventLoop::~EventLoop() {
  if (event_base_) {
    ::event_base_free(event_base_);
    event_base_ = nullptr;
  }
}

static void LibEventLog(int severity, const char *msg) {
  switch (severity) {
    case EVENT_LOG_DEBUG:
      std::cout << "DEBUG: " << msg;
      break;
    case EVENT_LOG_MSG:
      std::cout << "INFO: " << msg;
      break;
    case EVENT_LOG_WARN:
      std::cout << "WARN: " << msg;
      break;
  case EVENT_LOG_ERR:
    default:
      std::cout << "ERR: " << msg;
      break;
  }
}

bool EventLoop::Initialize() {
  if (signal(SIGHUP, SIG_IGN) == SIG_ERR) {
    std::cerr << "Ignore SIGHUP failed.";
    return false;
  }

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    std::cerr << "Ignore SIGPIPE failed.";
    return false;
  }

  ::event_set_log_callback(&LibEventLog);
  return true;
}

int EventLoop::Run() {
  if (!Initialize()) {
    return -1;
  }

  return event_base_dispatch(event_base_);
}

int EventLoop::Stop(bool immediate) {
  if (!event_base_)
    return 0;

  if (immediate)
    return ::event_base_loopbreak(event_base_);

  return ::event_base_loopexit(event_base_, nullptr);
}

event_base* EventLoop::GetEventBase() const {
  return event_base_;
}

}  // namespace EventBase
