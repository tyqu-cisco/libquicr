// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <condition_variable>
#include <oss/cxxopts.hpp>
#include <set>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

#include <quicr/server.h>

#include "signal_handler.h"

using TrackNamespaceHash = uint64_t;
using TrackNameHash = uint64_t;
using FullTrackNameHash = uint64_t;

namespace qserver_vars {
    std::mutex state_mutex;

    /**
     * Map of subscribes (e.g., track alias) sent to announcements
     *
     * @example
     *      track_alias_set = announce_active[track_namespace_hash][connection_handle]
     */
    std::unordered_map<TrackNamespaceHash,
                       std::unordered_map<quicr::ConnectionHandle, std::set<quicr::messages::TrackAlias>>>
      announce_active;

    /**
     * Active subscriber publish tracks for a given track, indexed (keyed) by track_alias, connection handle
     *
     * @note This indexing intentionally prohibits per connection having more
     *           than one subscribe to a full track name.
     *
     * @example track_handler = subscribes[track_alias][connection_handle]
     */
    std::unordered_map<quicr::messages::TrackAlias,
                       std::unordered_map<quicr::ConnectionHandle, std::shared_ptr<quicr::PublishTrackHandler>>>
      subscribes;

    /**
     * Subscribe ID to alias mapping
     *      Used to lookup the track alias for a given subscribe ID
     *
     * @example
     *      track_alias = subscribe_alias_sub_id[conn_id][subscribe_id]
     */
    std::unordered_map<quicr::ConnectionHandle,
                       std::unordered_map<quicr::messages::SubscribeId, quicr::messages::TrackAlias>>
      subscribe_alias_sub_id;

    /**
     * Map of subscribes set by namespace and track name hash
     *      Set<subscribe_who> = subscribe_active[track_namespace_hash][track_name_hash]
     */
    struct SubscribeWho
    {
        uint64_t connection_handle;
        uint64_t subscribe_id;
        uint64_t track_alias;

        bool operator<(const SubscribeWho& other) const
        {
            return connection_handle < other.connection_handle && subscribe_id << other.subscribe_id;
        }
        bool operator==(const SubscribeWho& other) const
        {
            return connection_handle == other.connection_handle && subscribe_id == other.subscribe_id;
        }
        bool operator>(const SubscribeWho& other) const
        {
            return connection_handle > other.connection_handle && subscribe_id > other.subscribe_id;
        }
    };
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::set<SubscribeWho>>> subscribe_active;

    /**
     * Active publisher/announce subscribes that this relay has made to receive objects from publisher.
     *
     * @example
     *      track_delegate = pub_subscribes[track_alias][conn_id]
     */
    std::unordered_map<quicr::messages::TrackAlias,
                       std::unordered_map<quicr::ConnectionHandle, std::shared_ptr<quicr::SubscribeTrackHandler>>>
      pub_subscribes;
}

/**
 * @brief  Subscribe track handler
 * @details Subscribe track handler used for the subscribe command line option.
 */
class MySubscribeTrackHandler : public quicr::SubscribeTrackHandler
{
  public:
    MySubscribeTrackHandler(const quicr::FullTrackName& full_track_name)
      : SubscribeTrackHandler(full_track_name)
    {
    }

    void ObjectReceived(const quicr::ObjectHeaders& object_headers, quicr::BytesSpan data) override
    {

        if (data.size() > 255) {
            SPDLOG_CRITICAL("Example server is for example only, received data > 255 bytes is not allowed!");
            SPDLOG_CRITICAL("Use github.com/quicr/laps for full relay functionality");
            throw std::runtime_error("Example server is for example only, received data > 255 bytes is not allowed!");
        }

        std::lock_guard<std::mutex> _(qserver_vars::state_mutex);

        auto track_alias = GetTrackAlias();
        if (!track_alias.has_value()) {
            SPDLOG_DEBUG("Data without valid track alias");
            return;
        }

        auto sub_it = qserver_vars::subscribes.find(track_alias.value());

        if (sub_it == qserver_vars::subscribes.end()) {
            SPDLOG_INFO("No subscribes, not relaying data size: {0} ", data.size());
            return;
        }

        for (const auto& [conn_id, pth] : sub_it->second) {
            pth->PublishObject(object_headers, data);
        }
    }

    void StatusChanged(Status status) override
    {
        if (status == Status::kOk) {
            SPDLOG_INFO("Track alias: {0} is subscribed", GetTrackAlias().value());
        } else {
            std::string reason = "";
            switch (status) {
                case Status::kNotConnected:
                    reason = "not connected";
                    break;
                case Status::kSubscribeError:
                    reason = "subscribe error";
                    break;
                case Status::kNotAuthorized:
                    reason = "not authorized";
                    break;
                case Status::kNotSubscribed:
                    reason = "not subscribed";
                    break;
                case Status::kPendingSubscribeResponse:
                    reason = "pending subscribe response";
                    break;
                case Status::kSendingUnsubscribe:
                    reason = "unsubscribing";
                    break;
                default:
                    break;
            }
            SPDLOG_INFO("Track alias: {0} failed to subscribe reason: {1}", GetTrackAlias().value(), reason);
        }
    }
};

/**
 * @brief Publish track handler
 * @details Publish track handler used for the publish command line option
 */
class MyPublishTrackHandler : public quicr::PublishTrackHandler
{
  public:
    MyPublishTrackHandler(const quicr::FullTrackName& full_track_name,
                          quicr::TrackMode track_mode,
                          uint8_t default_priority,
                          uint32_t default_ttl)
      : quicr::PublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl)
    {
    }

    void StatusChanged(Status status) override
    {
        if (status == Status::kOk) {
            SPDLOG_INFO("Publish track alias {0} has subscribers", GetTrackAlias().value());
        } else {
            std::string reason = "";
            switch (status) {
                case Status::kNotConnected:
                    reason = "not connected";
                    break;
                case Status::kNotAnnounced:
                    reason = "not announced";
                    break;
                case Status::kAnnounceNotAuthorized:
                    reason = "not authorized";
                    break;
                case Status::kPendingAnnounceResponse:
                    reason = "pending announce response";
                    break;
                case Status::kNoSubscribers:
                    reason = "no subscribers";
                    break;
                case Status::kSendingUnannounce:
                    reason = "sending unannounce";
                    break;
                default:
                    break;
            }
            SPDLOG_INFO("Publish track alias: {0} not ready, reason: {1}", GetTrackAlias().value(), reason);
        }
    }

    void MetricsSampled(const quicr::PublishTrackMetrics& metrics) override
    {
        SPDLOG_DEBUG("Metrics sample time: {0}"
                     " track_alias: {1}"
                     " objects sent: {2}"
                     " bytes sent: {3}"
                     " object duration us: {4}"
                     " queue discards: {5}"
                     " queue size: {6}",
                     metrics.last_sample_time,
                     GetTrackAlias().value(),
                     metrics.objects_published,
                     metrics.bytes_published,
                     metrics.quic.tx_object_duration_us.avg,
                     metrics.quic.tx_queue_discards,
                     metrics.quic.tx_queue_size.avg);
    }
};

/**
 * @brief MoQ Server
 * @details Implementation of the MoQ Server
 */
class MyServer : public quicr::Server
{
  public:
    MyServer(const quicr::ServerConfig& cfg)
      : quicr::Server(cfg)
    {
    }

    void NewConnectionAccepted(quicr::ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote) override
    {
        SPDLOG_INFO("New connection handle {0} accepted from {1}:{2}", connection_handle, remote.ip, remote.port);
    }

    void MetricsSampled(quicr::ConnectionHandle connection_handle, const quicr::ConnectionMetrics& metrics) override
    {
        SPDLOG_DEBUG("Metrics sample time: {0}"
                     " connection handle: {1}"
                     " rtt_us: {2}"
                     " srtt_us: {3}"
                     " rate_bps: {4}"
                     " lost pkts: {5}",
                     metrics.last_sample_time,
                     connection_handle,
                     metrics.quic.rtt_us.max,
                     metrics.quic.srtt_us.max,
                     metrics.quic.tx_rate_bps.max,
                     metrics.quic.tx_lost_pkts);
    }

    void UnannounceReceived(quicr::ConnectionHandle connection_handle,
                            const quicr::TrackNamespace& track_namespace) override
    {
        auto th = quicr::TrackHash({ track_namespace, {}, std::nullopt });

        SPDLOG_DEBUG("Received unannounce from connection handle: {0} for namespace hash: {1}, removing all tracks "
                     "associated with namespace",
                     connection_handle,
                     th.track_namespace_hash);

        for (auto track_alias : qserver_vars::announce_active[th.track_namespace_hash][connection_handle]) {
            auto ptd = qserver_vars::pub_subscribes[track_alias][connection_handle];
            if (ptd != nullptr) {
                SPDLOG_INFO(
                  "Received unannounce from connection handle: {0} for namespace hash: {1}, removing track alias: {2}",
                  connection_handle,
                  th.track_namespace_hash,
                  track_alias);

                UnsubscribeTrack(connection_handle, ptd);
            }
            qserver_vars::pub_subscribes[track_alias].erase(connection_handle);
            if (qserver_vars::pub_subscribes[track_alias].empty()) {
                qserver_vars::pub_subscribes.erase(track_alias);
            }
        }

        qserver_vars::announce_active[th.track_namespace_hash].erase(connection_handle);
        if (qserver_vars::announce_active[th.track_namespace_hash].empty()) {
            qserver_vars::announce_active.erase(th.track_namespace_hash);
        }
    }

    void AnnounceReceived(quicr::ConnectionHandle connection_handle,
                          const quicr::TrackNamespace& track_namespace,
                          const quicr::PublishAnnounceAttributes&) override
    {
        auto th = quicr::TrackHash({ track_namespace, {}, std::nullopt });

        SPDLOG_INFO("Received announce from connection handle: {0} for namespace_hash: {1}",
                    connection_handle,
                    th.track_namespace_hash);

        // Add to state if not exist
        auto [anno_conn_it, is_new] =
          qserver_vars::announce_active[th.track_namespace_hash].try_emplace(connection_handle);

        if (!is_new) {
            SPDLOG_INFO("Received announce from connection handle: {0} for namespace hash: {0} is duplicate, ignoring",
                        connection_handle,
                        th.track_namespace_hash);
            return;
        }

        AnnounceResponse announce_response;
        announce_response.reason_code = quicr::Server::AnnounceResponse::ReasonCode::kOk;
        ResolveAnnounce(connection_handle, track_namespace, announce_response);

        auto& anno_tracks = qserver_vars::announce_active[th.track_namespace_hash][connection_handle];

        // Check if there are any subscribes. If so, send subscribe to announce for all tracks matching namespace
        const auto sub_active_it = qserver_vars::subscribe_active.find(th.track_namespace_hash);
        if (sub_active_it != qserver_vars::subscribe_active.end()) {
            for (const auto& [track_name, who] : sub_active_it->second) {
                if (who.size()) { // Have subscribes
                    auto& a_who = *who.begin();
                    if (anno_tracks.find(a_who.track_alias) == anno_tracks.end()) {
                        SPDLOG_INFO("Sending subscribe to announcer connection handle: {0} subscribe track_alias: {1}",
                                    connection_handle,
                                    a_who.track_alias);

                        anno_tracks.insert(a_who.track_alias); // Add track to state

                        const auto pub_track_h = qserver_vars::subscribes[a_who.track_alias][a_who.connection_handle];

                        auto sub_track_handler =
                          std::make_shared<MySubscribeTrackHandler>(pub_track_h->GetFullTrackName());

                        SubscribeTrack(connection_handle, sub_track_handler);
                        qserver_vars::pub_subscribes[a_who.track_alias][connection_handle] = sub_track_handler;
                    }
                }
            }
        }
    }

    void ConnectionStatusChanged(quicr::ConnectionHandle connection_handle, ConnectionStatus status) override
    {
        if (status == ConnectionStatus::kConnected) {
            SPDLOG_DEBUG("Connection ready connection_handle: {0} ", connection_handle);
        } else {
            SPDLOG_DEBUG(
              "Connection changed connection_handle: {0} status: {1}", connection_handle, static_cast<int>(status));
        }
    }

    ClientSetupResponse ClientSetupReceived(quicr::ConnectionHandle,
                                            const quicr::ClientSetupAttributes& client_setup_attributes) override
    {
        ClientSetupResponse client_setup_response;

        SPDLOG_INFO("Client setup received from endpoint_id: {0}", client_setup_attributes.endpoint_id);

        return client_setup_response;
    }

    void UnsubscribeReceived(quicr::ConnectionHandle connection_handle, uint64_t subscribe_id) override
    {
        SPDLOG_INFO("Unsubscribe connection handle: {0} subscribe_id: {1}", connection_handle, subscribe_id);

        auto ta_conn_it = qserver_vars::subscribe_alias_sub_id.find(connection_handle);
        if (ta_conn_it == qserver_vars::subscribe_alias_sub_id.end()) {
            SPDLOG_WARN("Unable to find track alias connection for connection handle: {0} subscribe_id: {1}",
                        connection_handle,
                        subscribe_id);
            return;
        }

        auto ta_it = ta_conn_it->second.find(subscribe_id);
        if (ta_it == ta_conn_it->second.end()) {
            SPDLOG_WARN("Unable to find track alias for connection handle: {0} subscribe_id: {1}",
                        connection_handle,
                        subscribe_id);
            return;
        }

        std::lock_guard<std::mutex> _(qserver_vars::state_mutex);

        auto track_alias = ta_it->second;

        ta_conn_it->second.erase(ta_it);
        if (!ta_conn_it->second.size()) {
            qserver_vars::subscribe_alias_sub_id.erase(ta_conn_it);
        }

        auto& track_h = qserver_vars::subscribes[track_alias][connection_handle];

        if (track_h == nullptr) {
            SPDLOG_WARN("Unsubscribe unable to find track delegate for connection handle: {0} subscribe_id: {1}",
                        connection_handle,
                        subscribe_id);
            return;
        }

        auto th = quicr::TrackHash(track_h->GetFullTrackName());

        qserver_vars::subscribes[track_alias].erase(connection_handle);
        bool unsub_pub{ false };
        if (!qserver_vars::subscribes[track_alias].size()) {
            unsub_pub = true;
            qserver_vars::subscribes.erase(track_alias);
        }

        qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].erase(
          qserver_vars::SubscribeWho{ connection_handle, subscribe_id, th.track_fullname_hash });

        if (!qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].size()) {
            qserver_vars::subscribe_active[th.track_namespace_hash].erase(th.track_name_hash);
        }

        if (!qserver_vars::subscribe_active[th.track_namespace_hash].size()) {
            qserver_vars::subscribe_active.erase(th.track_namespace_hash);
        }

        if (unsub_pub) {
            SPDLOG_INFO("No subscribers left, unsubscribe publisher track_alias: {0}", track_alias);

            auto anno_ns_it = qserver_vars::announce_active.find(th.track_namespace_hash);
            if (anno_ns_it == qserver_vars::announce_active.end()) {
                return;
            }

            for (auto& [pub_connection_handle, tracks] : anno_ns_it->second) {
                if (tracks.find(th.track_fullname_hash) != tracks.end()) {
                    SPDLOG_INFO("Unsubscribe to announcer conn_id: {0} subscribe track_alias: {1}",
                                pub_connection_handle,
                                th.track_fullname_hash);

                    tracks.erase(th.track_fullname_hash); // Add track alias to state

                    auto sub_track_h = qserver_vars::pub_subscribes[th.track_fullname_hash][pub_connection_handle];
                    if (sub_track_h != nullptr) {
                        UnsubscribeTrack(pub_connection_handle, sub_track_h);
                    }
                }
            }
        }
    }

    void SubscribeReceived(quicr::ConnectionHandle connection_handle,
                           uint64_t subscribe_id,
                           [[maybe_unused]] uint64_t proposed_track_alias,
                           const quicr::FullTrackName& track_full_name,
                           const quicr::SubscribeAttributes&) override
    {
        auto th = quicr::TrackHash(track_full_name);

        SPDLOG_INFO("New subscribe connection handle: {0} subscribe_id: {1} track alias: {2}",
                    connection_handle,
                    subscribe_id,
                    th.track_fullname_hash);

        auto pub_track_h = std::make_shared<MyPublishTrackHandler>(track_full_name, quicr::TrackMode::kStream, 2, 5000);
        qserver_vars::subscribes[th.track_fullname_hash][connection_handle] = pub_track_h;
        qserver_vars::subscribe_alias_sub_id[connection_handle][subscribe_id] = th.track_fullname_hash;

        // record subscribe as active from this subscriber
        qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].emplace(
          qserver_vars::SubscribeWho{ connection_handle, subscribe_id, th.track_fullname_hash });

        // Create a subscribe track that will be used by the relay to send to subscriber for matching objects
        BindPublisherTrack(connection_handle, subscribe_id, pub_track_h);

        // Subscribe to announcer if announcer is active
        auto anno_ns_it = qserver_vars::announce_active.find(th.track_namespace_hash);
        if (anno_ns_it == qserver_vars::announce_active.end()) {
            SPDLOG_INFO("Subscribe to track namespace hash: {0}, does not have any announcements.",
                        th.track_namespace_hash);
            return;
        }

        for (auto& [conn_h, tracks] : anno_ns_it->second) {
            if (tracks.find(th.track_fullname_hash) == tracks.end()) {
                SPDLOG_INFO("Sending subscribe to announcer connection handler: {0} subscribe track_alias: {1}",
                            conn_h,
                            th.track_fullname_hash);

                tracks.insert(th.track_fullname_hash); // Add track alias to state

                auto sub_track_h = std::make_shared<MySubscribeTrackHandler>(track_full_name);
                SubscribeTrack(conn_h, sub_track_h);
                qserver_vars::pub_subscribes[th.track_fullname_hash][conn_h] = sub_track_h;
            }
        }
    }
};

/* -------------------------------------------------------------------------------------------------
 * Main program
 * -------------------------------------------------------------------------------------------------
 */
quicr::ServerConfig
InitConfig(cxxopts::ParseResult& cli_opts)
{
    quicr::ServerConfig config;

    std::string qlog_path;
    if (cli_opts.count("qlog")) {
        qlog_path = cli_opts["qlog"].as<std::string>();
    }

    if (cli_opts.count("debug") && cli_opts["debug"].as<bool>() == true) {
        SPDLOG_INFO("setting debug level");
        spdlog::default_logger()->set_level(spdlog::level::debug);
    }

    if (cli_opts.count("version") && cli_opts["version"].as<bool>() == true) {
        SPDLOG_INFO("QuicR library version: {}", QUICR_VERSION);
        exit(0);
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();

    config.server_bind_ip = cli_opts["bind_ip"].as<std::string>();
    config.server_port = cli_opts["port"].as<uint16_t>();

    config.transport_config.debug = cli_opts["debug"].as<bool>();
    config.transport_config.tls_cert_filename = cli_opts["cert"].as<std::string>();
    config.transport_config.tls_key_filename = cli_opts["key"].as<std::string>();
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.quic_qlog_path = qlog_path;

    return config;
}

int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    cxxopts::Options options("qclient",
                             std::string("MOQ Example Server using QuicR Version: ") + std::string(QUICR_VERSION));
    options.set_width(75).set_tab_expansion().allow_unrecognised_options().add_options()("h,help", "Print help")(
      "d,debug", "Enable debugging") // a bool parameter
      ("v,version", "QuicR Version") // a bool parameter
      ("b,bind_ip", "Bind IP", cxxopts::value<std::string>()->default_value("127.0.0.1"))(
        "p,port", "Listening port", cxxopts::value<uint16_t>()->default_value("1234"))(
        "e,endpoint_id", "This relay/server endpoint ID", cxxopts::value<std::string>()->default_value("moq-server"))(
        "c,cert", "Certificate file", cxxopts::value<std::string>()->default_value("./server-cert.pem"))(
        "k,key", "Certificate key file", cxxopts::value<std::string>()->default_value("./server-key.pem"))(
        "q,qlog", "Enable qlog using path", cxxopts::value<std::string>()); // end of options

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help({ "" }) << std::endl;
        return EXIT_SUCCESS;
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock<std::mutex> lock(moq_example::main_mutex);

    quicr::ServerConfig config = InitConfig(result);

    try {
        auto server = std::make_shared<MyServer>(config);
        if (server->Start() != quicr::Transport::Status::kReady) {
            SPDLOG_ERROR("Server failed to start");
            exit(-2);
        }

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

        // Unlock the mutex
        lock.unlock();
    } catch (const std::invalid_argument& e) {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unexpected exception" << std::endl;
        result_code = EXIT_FAILURE;
    }

    return result_code;
}
