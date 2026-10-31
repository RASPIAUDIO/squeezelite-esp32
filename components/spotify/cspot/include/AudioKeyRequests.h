#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

namespace cspot {
// Caller serializes access. All times are monotonic milliseconds. Never sleeps
// or invokes callbacks: the owner dispatches completions outside its mutex.
// Policy adapted from librespot PR 1844; not a cure for persistent refusals.
class AudioKeyRequests {
 public:
  using Callback = std::function<void(bool, const std::vector<uint8_t>&)>;
  enum class Kind { None, Send, Retry, Complete };
  struct Event {
    Kind kind = Kind::None;
    uint32_t wire = 0;
    unsigned attempt = 0;
    uint64_t waitMs = 0;
    bool success = false;
    Callback callback;
    std::vector<uint8_t> track, file, response;
  };

  uint32_t add(const std::vector<uint8_t>& track,
               const std::vector<uint8_t>& file, Callback callback) {
    const auto id = nextId++;
    entries.push_back({id, track, file, std::move(callback), 0});
    return id;
  }

  Event poll(uint64_t now) {
    if (activeWire) {
      if (now < deadline) return {};
      return finish(false, {});  // Timeout is not an explicit refusal.
    }
    if (entries.empty() || now < nextAllowed) return {};
    auto& entry = entries.front();
    if (entry.track.size() != 16 || entry.file.size() != 20)
      return finish(false, {});
    activeWire = nextWire++;
    deadline = now + 5000;
    nextAllowed = now + 200;
    Event event;
    event.kind = Kind::Send;
    event.wire = activeWire;
    event.attempt = ++entry.attempts;
    event.track = entry.track;
    event.file = entry.file;
    return event;
  }

  Event reply(uint32_t wire, bool success,
              const std::vector<uint8_t>& response, uint64_t now) {
    if (!activeWire || wire != activeWire || entries.empty()) return {};
    if (success) {
      if (response.size() != 20) return finish(false, {});
      refusals = 0;
      return finish(true, response);
    }
    static constexpr uint64_t waits[] = {3000, 5000, 10000};
    const auto delay = waits[std::min(refusals, 2u)];
    if (refusals < 3) ++refusals;
    nextAllowed = std::max(nextAllowed, now + delay);
    activeWire = 0;
    if (entries.front().attempts >= 4) return finish(false, {});
    Event event;
    event.kind = Kind::Retry;
    event.attempt = entries.front().attempts + 1;
    event.waitMs = delay;
    return event;
  }

  Event sendFailed(uint32_t wire) {
    if (!activeWire || wire != activeWire) return {};
    return finish(false, {});
  }

  void cancel(uint32_t id) {
    auto it = std::find_if(entries.begin(), entries.end(),
                           [id](const Entry& e) { return e.id == id; });
    if (it == entries.end()) return;
    if (it == entries.begin()) activeWire = 0;
    entries.erase(it);
    // Keep the session cooldown; cancelling/skipping cannot bypass it.
  }

  std::vector<Event> abort() {
    std::vector<Event> result;
    while (!entries.empty()) result.push_back(finish(false, {}));
    // Keep sequence counters and cooldown across reconnects. Late replies
    // must never be mistaken for a new request.
    return result;
  }

 private:
  struct Entry {
    uint32_t id;
    std::vector<uint8_t> track, file;
    Callback callback;
    unsigned attempts;
  };
  std::deque<Entry> entries;
  uint32_t nextId = 1, nextWire = 1, activeWire = 0;
  uint64_t deadline = 0, nextAllowed = 0;
  unsigned refusals = 0;

  Event finish(bool success, const std::vector<uint8_t>& response) {
    Event event;
    event.kind = Kind::Complete;
    event.success = success;
    event.attempt = entries.front().attempts;
    event.callback = std::move(entries.front().callback);
    event.response = response;
    entries.pop_front();
    activeWire = 0;
    return event;
  }
};
}  // namespace cspot
