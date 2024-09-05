// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include <moq/client.h>
#include <moq/common.h>
#include <moq/publish_track_handler.h>
#include <moq/server.h>
#include <moq/subscribe_track_handler.h>

class TestPublishTrackHandler : public moq::PublishTrackHandler
{
    TestPublishTrackHandler()
      : PublishTrackHandler({ {}, {}, std::nullopt }, moq::TrackMode::kStreamPerGroup, 0, 0)
    {
    }

  public:
    static std::shared_ptr<TestPublishTrackHandler> Create()
    {
        return std::shared_ptr<TestPublishTrackHandler>(new TestPublishTrackHandler());
    }
};

TEST_CASE("Create Track Handler")
{
    CHECK_NOTHROW(moq::PublishTrackHandler::Create({ {}, {}, std::nullopt }, moq::TrackMode::kStreamPerGroup, 0, 0));
    CHECK_NOTHROW(TestPublishTrackHandler::Create());
}
