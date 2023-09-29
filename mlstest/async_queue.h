#pragma once

#include <condition_variable>
#include <queue>

template<typename T>
struct AsyncQueue
{
  bool empty() const {
    std::unique_lock<std::mutex> lock(mutex);
    return queue.empty();
  }

  void push(const T& val)
  {
    std::unique_lock<std::mutex> lock(mutex);
    queue.push(val);
    lock.unlock();
    nonempty.notify_all();
  }

  T pop()
  {
    std::unique_lock<std::mutex> lock(mutex);
    nonempty.wait(lock, [&] { return !queue.empty(); });
    const auto val = queue.front();
    queue.pop();
    return val;
  }

  std::optional<T> pop(std::chrono::milliseconds wait_time)
  {
    std::unique_lock<std::mutex> lock(mutex);
    const auto success =
      nonempty.wait_for(lock, wait_time, [&] { return !queue.empty(); });
    if (!success) {
      return std::nullopt;
    }

    const auto val = queue.front();
    queue.pop();
    return val;
  }

  std::mutex mutex;
  std::condition_variable nonempty;
  std::queue<T> queue;
};
