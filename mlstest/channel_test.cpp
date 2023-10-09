#include <doctest/doctest.h>

#include "channel.h"

#include <thread>

using namespace std::chrono_literals;

TEST_CASE("Channel receive blocks on empty")
{
  const auto [send, recv] = channel::create<uint32_t>(1);

  auto recv_thread = std::thread([recv = std::move(recv)]() {
    REQUIRE(recv.receive() == 42);
  });

  std::this_thread::sleep_for(100ms);
  send.send(42);

  recv_thread.join();
}

TEST_CASE("Channel send blocks on full")
{
  const auto [send, recv] = channel::create<uint32_t>(1);

  REQUIRE(send.send(1)); // Should not block

  auto send_thread = std::thread([send = std::move(send)]() {
    REQUIRE(send.send(2)); // Should block
  });

  REQUIRE(recv.receive() == 1);

  send_thread.join(); // Should now be unblocked

  REQUIRE(recv.receive() == 2);
}

TEST_CASE("Channel receive blocks for bounded time")
{
  const auto [send, recv] = channel::create<uint32_t>(1);

  REQUIRE(recv.receive(100ms) == std::nullopt);
}

TEST_CASE("Channel send blocks for bounded time")
{
  const auto [send, recv] = channel::create<uint32_t>(1);

  REQUIRE(send.send(42));
  REQUIRE_FALSE(send.send(42, 100ms));
}
