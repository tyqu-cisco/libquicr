/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <moqt/config.h>
#include <moqt/core/transport.h>
#include <moqt/core/messages.h>

namespace moq::transport {
    using namespace qtransport;

    /**
     * @brief MoQT Server
     *
     * @details MoQT Server is the handler of the MoQT QUIC listening socket
     */
    class Server : public Transport
    {
      public:
        /**
         * @brief MoQ Server constructor to create the MOQ server mode instance
         *
         * @param cfg           MoQT Server Configuration
         */
        Server(const ServerConfig& cfg)
          : Transport(cfg)
        {
        }

        ~Server() = default;

        /**
         * @brief Starts server transport thread to listen for new connections
         *
         * @details Creates a new transport thread to listen for new connections. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    READY.
         */
        Status Start();

        /**
         * Stop the server transport
         */
        void Stop() { stop_ = true; }

        /**
         * @brief Callback notification on new connection
         * @details Callback notification that a new connection has been accepted
         *
         * @param conn_id          Transport connection ID
         * @param remote           Transport remote connection information
         */
        virtual void NewConnection([[maybe_unused]] TransportConnId conn_id,
                                   [[maybe_unused]] const TransportRemote& remote) = 0;

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param conn_id          Transport connection ID
         * @param status           Transport status of connection id
         */
        virtual void ConnectionChanged(TransportConnId conn_id, TransportStatus status) = 0;

        /**
         * @brief Callback on client setup message
         * @details In server mode, client will send a setup message on new connection.
         *         Server responds with server setup.
         *
         * @param conn_id          Transport connection ID
         * @param client_setup     Decoded client setup message
         */
        virtual void ClientSetupReceived([[maybe_unused]] TransportConnId conn_id,
                                         [[maybe_unused]] messages::MoqClientSetup client_setup) = 0;

        /**
         * @brief Callback notification for new announce received that needs to be authorized
         *
         * @param conn_id                   Source connection ID
         * @param track_namespace           Track namespace
         *
         * @return True if authorized and announce OK will be sent, false if not
         */
        virtual bool AnnounceReceived([[maybe_unused]] TransportConnId conn_id,
                                      [[maybe_unused]] const std::vector<uint8_t>& track_namespace) = 0;

        /**
         * @brief Callback notification for unannounce received
         *
         * @param conn_id                   Source connection ID
         * @param track_namespace           Track namespace
         *
         */
        virtual void UnannounceReceived([[maybe_unused]] TransportConnId conn_id,
                                        [[maybe_unused]] const std::vector<uint8_t>& track_namespace) = 0;

        /**
         * @brief Callback notification for new subscribe received
         *
         * @param conn_id             Source connection ID
         * @param subscribe_id        Subscribe ID received
         * @param track_namespace     Track Namespace from subscription
         * @param track_name          Track name from subscription
         *
         * @return True if send announce should be sent, false if not
         */
        virtual bool SubscribeReceived([[maybe_unused]] TransportConnId conn_id,
                                       [[maybe_unused]] uint64_t subscribe_id,
                                       [[maybe_unused]] const std::vector<uint8_t>& track_namespace,
                                       [[maybe_unused]] const std::vector<uint8_t>& track_name) = 0;

        /**
         * @brief Callback notification on unsubscribe received
         *
         * @param conn_id             Source connection ID
         * @param subscribe_id        Subscribe ID received
         */
        virtual void UnsubscribeReceived([[maybe_unused]] TransportConnId conn_id,
                                         [[maybe_unused]] uint64_t subscribe_id) = 0;

      private:
        bool stop_ { false };
    };

} // namespace moq::transport
