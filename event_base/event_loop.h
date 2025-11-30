#pragma once  // NOLINT(build/header_guard)

#include <event2/event.h>

namespace EventBase {

class EventLoop {
 public:
  EventLoop();
  ~EventLoop();

  // Return value:
  // 0 means success, otherwise, -1
  int Run();

  // Return value:
  // 0 means success, otherwise, -1
  // if |immediate| is true, loop will stop immediately
  // after current processing.
  // Otherwise, loop will continue running all events in this round then stop.
  int Stop(bool immediate = false);

  event_base* GetEventBase() const;

 private:
  bool Initialize();

 private:
  event_base *event_base_;
};

}  // namespace EventBase
