/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/moqt_messages.h>
#include <transport/transport.h>

namespace quicr {
    using namespace qtransport;

    /**
     * @brief MoQ/MOQT client callbacks
     *
     * @details MoQ client callback delegate for connection and MOQT control message handling.
     */
    class MOQTClientCallbacks
    {
      public:
        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param conn_id          Transport connection ID
         * @param endpoint_id      Endpoint ID of remote side
         * @param status           Transport status of connection id
         */
        virtual void connectionStatus(TransportConnId conn_id,
                                      std::span<uint8_t const> endpoint_id,
                                      TransportStatus status) = 0;

        /**
         * @brief Callback on server setup message
         * @details Server will send sever setup in response to client setup message sent. This callback is called
         *  when a server setup has been received.
         *
         * @param conn_id          Transport connection ID
         * @param server_setup     Decoded sever setup message
         */
        virtual void serverSetup([[maybe_unused]] TransportConnId conn_id,
                                 [[maybe_unused]] messages::MoqServerSetup server_setup)
        {
        }

    };

} // namespace quicr
