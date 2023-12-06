/*
 *  quicr_server_delegate.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This defined the server delegate interface utilized by the library to
 *      deliver information to the application.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include "quicr/encode.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_common.h"

#include <transport/transport.h>

#include <cstdint>
#include <string>

namespace quicr {

/**
 * Server delegate QUICR callback methods implemented by the QUICR Server
 * implementation
 */
class ServerDelegate
{
public:
  ServerDelegate() = default;
  virtual ~ServerDelegate() = default;

  /**
   * @brief Reports intent to publish under given quicr::Name.
   *
   * @param namespace               : Identifies QUICR namespace
   * @param origin_url              : Origin serving the QUICR Session
   * @param auth_token              : Auth Token to validate subscribe requests
   * @param e2e_token               : Opaque token to be forwarded to the Origin
   *
   * @details Entities processing the Publish Intent MUST validate the request
   *           against the auth_token, verify if the Origin specified
   *           in the origin_url is trusted and forward the request to the
   *           next hop Relay for that
   *           Origin or to the Origin (if it is the next hop) unless the entity
   *           itself the Origin server.
   *           It is expected for the Relays to store the publisher state
   * mapping the namespaces and other relation information.
   */
  virtual void onPublishIntent(const quicr::Namespace& quicr_name,
                               const std::string& origin_url,
                               const std::string& auth_token,
                               bytes&& e2e_token) = 0;

  /**
   * @brief Reports intent to publish for name is ended.
   *
   * @param quicr_namespace : The namespace which we want to end intent for.
   * @param auth_token      : Auth token to validate request.
   * @param e2e_token       : Opaque token to be forwarded to the Origin
   */
  virtual void onPublishIntentEnd(const quicr::Namespace& quicr_namespace,
                                  const std::string& auth_token,
                                  bytes&& e2e_token) = 0;

  /**
   * @brief Reports arrival of fully assembled QUICR object under the name
   *
   * @param conn_id               : Context id the message was received on
   * @param data_ctx_id           : Stream ID the message was received on
   * @param datagram              : QuicR Published Message Datagram
   *
   * @note: It is important that the implementations not perform
   *         compute intensive tasks in this callback, but rather
   *         copy/move the needed information and hand back the control
   *         to the stack
   *
   *  @note: Both the on_publish_object and on_publish_object_fragment
   *         callbacks will be called. The delegate implementation
   *         shall decide the right callback for their usage.
   */
  virtual void onPublisherObject(const qtransport::TransportConnId& conn_id,
                                 const qtransport::DataContextId& data_ctx_id,
                                 messages::PublishDatagram&& datagram) = 0;

  /**
   * @brief Report arrival of subscribe request for a QUICR Namespace
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * 		request against the token, verify if the Origin specified in the
   * origin_url is trusted and forward the request to the next hop Relay for
   * that Origin or to the Origin (if it is the next hop) unless the entity
   *    itself the Origin server.
   *    It is expected for the Relays to store the subscriber state
   *    mapping the subscribe context, namespaces and other relation
   * information.
   *
   * @param namespace             : Identifies QUICR namespace
   * @param subscriber_id           Subscriber ID connection/transport that
   *                                sent the message
   * @param conn_id               : Context id the message was received on
   * @param data_ctx_id           : Stream ID the message was received on
   * @param subscribe_intent      : Subscribe intent to determine the start
   *                                point for serving the matched objects.
   *                                The application may choose a different intent
   *                                mode, but must be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session
   * @param auth_token            : Auth Token to valiadate the Subscribe
   * Request
   * @param payload               : Opaque payload to be forwarded to the Origin
   *
   */
  virtual void onSubscribe(const quicr::Namespace& quicr_namespace,
                           const uint64_t& subscriber_id,
                           const qtransport::TransportConnId& conn_id,
                           const qtransport::DataContextId& data_ctx_id,
                           const SubscribeIntent subscribe_intent,
                           const std::string& origin_url,
                           const std::string& auth_token,
                           bytes&& data) = 0;

  /**
   * @brief Unsubscribe callback method
   *
   * @details Called for each unsubscribe message
   *
   * @param quicr_namespace          QuicR name/len
   * @param subscriber_id            Subscriber ID connection/transport that
   *                                 sent the message
   * @param auth_token               Auth token to verify if value
   */
  virtual void onUnsubscribe(const quicr::Namespace& quicr_namespace,
                             const uint64_t& subscriber_id,
                             const std::string& auth_token) = 0;
};

} // namespace quicr
