/*
 *  quicr_client_raw_session.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This file defines a session layer between the client APIs and the
 *      transport that uses raw data packets, namely UDP or QUIC.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include "quicr/encode.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_client.h"
#include "quicr/quicr_client_delegate.h"
#include "quicr/quicr_common.h"

#include <qname>
#include <transport/transport.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>

namespace quicr {

/**
 *   Client Raw Session Interface
 */
class ClientRawSession
  : public ClientSession
  , public qtransport::ITransport::TransportDelegate
{
public:
  /**
   * @brief Setup a QUICR Client Session with publisher and subscriber
   *        functionality.
   *
   * @param relayInfo : Relay Information to be used by the transport
   * @param tconfig   : Transport configuration
   * @param logger    : Shared pointer to a cantina::Logger object
   *
   * @throws std::runtime_error : If transport fails to connect.
   */
  ClientRawSession(const RelayInfo& relay_info,
                   const qtransport::TransportConfig& tconfig,
                   const cantina::LoggerPointer& logger);

  /**
   * @brief Setup a QUICR Client Session with publisher and subscriber
   *        functionality.
   *
   * @param transport : External transport pointer to use.
   * @param logger    : Shared pointer to a cantina::Logger object
   */
  ClientRawSession(std::shared_ptr<qtransport::ITransport> transport,
                   const cantina::LoggerPointer& logger);

  /**
   * @brief Destructor for the raw client session object
   */
  virtual ~ClientRawSession();

  /**
   * @brief Connects the session using the info provided on construction.
   * @returns True if connected, false otherwise.
   */
  virtual bool connect() override;

  /**
   * @brief Disconnects the session from the relay.
   * @returns True if successful, false if some error occurred.
   */
  virtual bool disconnect() override;

  /**
   * @brief Get the client status
   *
   * @details This method should be used to determine if the client is
   *   connected and ready for publishing and subscribing to messages.
   *   Status will indicate the type of error if not ready.
   *
   * @returns client status
   */
  ClientStatus status() const override { return client_status; }

  /**
   * @brief Publish intent to publish on a QUICR Namespace
   *
   * @param pub_delegate            : Publisher delegate reference
   * @param quicr_namespace         : Identifies QUICR namespace
   * @param origin_url              : Origin serving the QUICR Session
   * @param auth_token              : Auth Token to validate the Subscribe
   * Request
   * @param payload                 : Opaque payload to be forwarded to the
   * Origin
   * @param use_reliable_transport  : Indicates to use reliable for matching
   * published objects
   */
  bool publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                     const quicr::Namespace& quicr_namespace,
                     const std::string& origin_url,
                     const std::string& auth_token,
                     bytes&& payload,
                     bool use_reliable_transport) override;

  /**
   * @brief Stop publishing on the given QUICR namespace
   *
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param origin_url             : Origin serving the QUICR Session
   * @param auth_token             : Auth Token to valiadate the Subscribe
   * Request
   * @param payload                : Opaque payload to be forwarded to the
   * Origin
   */
  void publishIntentEnd(const quicr::Namespace& quicr_namespace,
                        const std::string& auth_token) override;

  /**
   * @brief Perform subscription operation a given QUICR namespace
   *
   * @param subscriber_delegate   : Reference to receive callback for subscriber
   *                                ooperations
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param subscribe_intent      : Subscribe intent to determine the start
   * point for serving the matched objects. The application may choose a
   * different intent mode, but must be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session
   * @param use_reliable_transport: Reliable or Unreliable transport
   * @param auth_token            : Auth Token to validate the Subscribe Request
   * @param e2e_token              : Opaque token to be forwarded to the Origin
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request against the token, verify if the Origin specified in the origin_url
   *          is trusted and forward the request to the next hop Relay for that
   *          Origin or to the Origin (if it is the next hop) unless the entity
   *          itself the Origin server.
   *          It is expected for the Relays to store the subscriber state
   * mapping the subscribe context, namespaces and other relation information.
   */
  void subscribe(std::shared_ptr<SubscriberDelegate> subscriber_delegate,
                 const quicr::Namespace& quicr_namespace,
                 const SubscribeIntent& intent,
                 const std::string& origin_url,
                 bool use_reliable_transport,
                 const std::string& auth_token,
                 bytes&& e2e_token) override;

  /**
   * @brief Stop subscription on the given QUICR namespace
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session
   * @param auth_token            : Auth Token to validate the Subscribe
   *                                Request
   */
  void unsubscribe(const quicr::Namespace& quicr_namespace,
                   const std::string& origin_url,
                   const std::string& auth_token) override;

  /**
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                      before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param data                     : Opaque payload
   *
   */
  void publishNamedObject(const quicr::Name& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bool use_reliable_transport,
                          bytes&& data) override;

  /**
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
                                       before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   * @param data                     : Opaque payload of the fragment
   */
  void publishNamedObjectFragment(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  bytes&& data) override;

protected:
  void on_connection_status(const qtransport::TransportContextId& context_id,
                            const qtransport::TransportStatus status) override;

  void on_new_connection(const qtransport::TransportContextId& context_id,
                         const qtransport::TransportRemote& remote) override;

  void on_new_stream(const qtransport::TransportContextId& context_id,
                     const qtransport::StreamId& mStreamId) override;

  void on_recv_notify(const qtransport::TransportContextId& context_id,
                      const qtransport::StreamId& streamId) override;

  static bool notify_pub_fragment(
    const messages::PublishDatagram& datagram,
    const std::shared_ptr<SubscriberDelegate>& delegate,
    const std::map<uint32_t, bytes>& frag_map);

  void handle_pub_fragment(messages::PublishDatagram&& datagram,
                           const std::shared_ptr<SubscriberDelegate>& delegate);

  void handle(messages::MessageBuffer&& msg);

  void removeSubscription(const quicr::Namespace& quicr_namespace,
                          const SubscribeResult::SubscribeStatus& reason);

protected:
  std::mutex session_mutex;

  // State to store per-subscribe context
  struct SubscribeContext
  {
    enum struct State
    {
      Unknown = 0,
      Pending,
      Ready
    };

    State state{ State::Unknown };
    qtransport::TransportContextId transport_context_id{ 0 };
    qtransport::StreamId transport_stream_id{ 0 };
    uint64_t transaction_id{ 0 };
    uint64_t last_group_id{ 0 };
    uint64_t last_object_id{ 0 };
  };

  // State per publish_intent and related publish
  struct PublishContext
  {
    enum struct State
    {
      Unknown = 0,
      Pending,
      Ready
    };

    State state{ State::Unknown };
    qtransport::TransportContextId transport_context_id{ 0 };
    qtransport::StreamId transport_stream_id{ 0 };
    uint64_t last_group_id{ 0 };
    uint64_t last_object_id{ 0 };
    uint64_t offset{ 0 };
  };

  bool need_pacing{ false };
  bool has_shared_transport{ false };
  std::atomic_bool stopping{ false };

  // These parameters are updated on connect() / disconnect().  The optional
  // parameters should be non-null if and only iff client_status == READY.
  ClientStatus client_status{ ClientStatus::TERMINATED };
  std::optional<qtransport::StreamId> transport_dgram_stream_id;
  std::optional<qtransport::TransportContextId> transport_context_id;

  /*
   * Nested map to reassemble message fragments
   *
   *    Structure:
   *       fragments[<circular index>] = map[quicr_name] = map[offset] = data
   *
   *    Circular index is a small int value that increments from 1 to max. It
   *    wraps to 1 after reaching max size.  In this sense, it's a circular
   *    buffer. Upon moving to a new index the new index data will be purged (if
   *    any exists).
   *
   *    Fragment reassembly avoids timers and time interval based checks. It
   *    instead is based on received data. Every message quicr_name is checked
   * to see if it's complete. If so, the published object callback will be
   *    executed. If not, it'll only update the map with the new offset value.
   *    Incomplete messages can exist in the cache for as long as the circular
   *    index hasn't wrapped to the same point in cache.  Under high
   * load/volume, this can wrap within a minute or two.  Under very little load,
   * this could linger for hours. This is okay considering the only harm is a
   * little extra memory being used. Extra memory is a trade-off for being
   * event/message driven instead of timer based with threading/locking/...
   */
  std::map<uint32_t, std::map<quicr::Name, std::map<uint32_t, bytes>>>
    fragments;

  cantina::LoggerPointer logger;

  namespace_map<std::shared_ptr<PublisherDelegate>> pub_delegates;
  namespace_map<PublishContext> publish_state{};

  namespace_map<std::shared_ptr<SubscriberDelegate>> sub_delegates;
  namespace_map<SubscribeContext> subscribe_state{};

  std::shared_ptr<qtransport::ITransport> transport;
};

}
