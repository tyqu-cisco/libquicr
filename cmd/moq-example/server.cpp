
#include <quicr/moq_instance.h>

#include <cantina/logger.h>
#include <unordered_map>
#include <condition_variable>
#include <csignal>
#include <oss/cxxopts.hpp>
#include "signal_handler.h"
#include "subscription.h"

namespace qserver_vars {
    /*
     * Active subscribes for a given track, indexed (keyed) by track_alias,conn_id
     *     NOTE: This indexing intentionally prohibits per connection having more
     *           than one subscribe to a full track name.
     *
     *     Example: track_delegate = subscribes[track_alias][conn_id]
     */
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::shared_ptr<quicr::MoQTrackDelegate>>> subscribes;

}

class subTrackDelegate : public quicr::MoQTrackDelegate
{
public:
    subTrackDelegate(const std::string& t_namespace,
                     const std::string& t_name,
                     uint8_t priority,
                     uint32_t ttl,
                     const cantina::LoggerPointer& logger)
      : MoQTrackDelegate({ t_namespace.begin(), t_namespace.end() },
                         { t_name.begin(), t_name.end() },
                         TrackMode::STREAM_PER_GROUP,
                         priority,
                         ttl,
                         logger)
    {
    }

    void cb_objectReceived(uint64_t group_id, uint64_t object_id, std::vector<uint8_t>&& object) override {}
    void cb_sendCongested(bool cleared, uint64_t objects_in_queue) override {}

    void cb_sendReady() override {
        _logger->info << "Track alias: " << _track_alias.value() << " is ready to send" << std::flush;
    }

    void cb_sendNotReady(TrackSendStatus status) override {}
    void cb_readReady() override
    {
        _logger->info << "Track alias: " << _track_alias.value() << " is ready to read" << std::flush;
    }
    void cb_readNotReady(TrackReadStatus status) override {}
};


class serverDelegate : public quicr::MoQInstanceDelegate
{
  public:
    serverDelegate(const cantina::LoggerPointer& logger) :
      _logger(std::make_shared<cantina::Logger>("MID", logger)) {}

    void set_moq_instance(std::weak_ptr<quicr::MoQInstance> moq_instance)
    {
        _moq_instance = moq_instance;
    }

    void cb_newConnection(qtransport::TransportConnId conn_id,
                          const std::span<uint8_t>& endpoint_id,
                          const qtransport::TransportRemote& remote) override {}

    bool cb_announce(qtransport::TransportConnId conn_id,
                     uint64_t track_namespace_hash) override {

        _logger->debug << "Received announce from conn_id: " << conn_id
                       << "  for namespace_hash: " << track_namespace_hash
                       << std::flush;

        // Send announce OK
        return true;
    }

    void cb_connectionStatus(qtransport::TransportConnId conn_id,
                             const std::span<uint8_t>& endpoint_id,
                             qtransport::TransportStatus status) override {
        auto ep_id = std::string(endpoint_id.begin(), endpoint_id.end());

        if (status == qtransport::TransportStatus::Ready) {
            _logger->debug << "Connection ready conn_id: " << conn_id
                           << " endpoint_id: " << ep_id
                           << std::flush;

        }
    }
    void cb_clientSetup(qtransport::TransportConnId conn_id, quicr::messages::MoqClientSetup client_setup) override {}
    void cb_serverSetup(qtransport::TransportConnId conn_id, quicr::messages::MoqServerSetup server_setup) override {}

    bool cb_subscribe(qtransport::TransportConnId conn_id,
                      uint64_t subscribe_id,
                      std::span<uint8_t const> name_space,
                      std::span<uint8_t const> name) override
    {
        std::string const t_namespace(name_space.begin(), name_space.end());
        std::string const t_name(name.begin(), name.end());

        _logger->info << "New subscribe conn_id: " << conn_id
                       << " subscribe_id: " << subscribe_id
                       << " track: " << t_namespace << "/" << t_name
                       << std::flush;

        auto track_delegate = std::make_shared<subTrackDelegate>(t_namespace, t_name, 2, 3000, _logger);
        auto tfn = quicr::MoQInstance::TrackFullName{ name_space, name };
        auto th = quicr::MoQInstance::TrackHash(tfn);
        qserver_vars::subscribes[th.track_fullname_hash][conn_id] = track_delegate;

        // Create a subscribe track that will be used by the relay to send to subscriber for matching objects
        _moq_instance.lock()->bindSubscribeTrack(conn_id, subscribe_id, track_delegate);

        return true;
    }

    void cb_objectReceived(qtransport::TransportConnId conn_id,
                           uint64_t subscribe_id,
                           uint64_t track_alias,
                           uint64_t group_id,
                           uint64_t object_id,
                           std::vector<uint8_t>&& data)
    {
        _logger->info << "Recieved object conn_id: " << conn_id
                      << " subscribe_id: " << subscribe_id
                      << " track_alias: " << track_alias
                      << " group_id: " << group_id
                      << " object_id: " << object_id
                      << " data size: " << data.size()
                      << std::flush;

        // Relay to all subscribes
        auto sub_it = qserver_vars::subscribes.find(track_alias);
        for (auto& [conn_id, track_delegate]: sub_it->second) {
            _logger->info << "Sending to conn_id: " << conn_id
                          << " subscribe_id: " << *track_delegate->getSubscribeId()
                          << std::flush;
            track_delegate->sendObject(group_id, object_id, data);
        }
    }

  private:
    cantina::LoggerPointer _logger;
    std::weak_ptr<quicr::MoQInstance> _moq_instance;
};

quicr::MoQInstanceServerConfig init_config(cxxopts::ParseResult& cli_opts, const cantina::LoggerPointer& logger)
{
    quicr::MoQInstanceServerConfig config;

    std::string qlog_path;
    if (cli_opts.count("qlog")) {
        qlog_path = cli_opts["qlog"].as<std::string>();
    }

    if (cli_opts.count("debug") && cli_opts["debug"].as<bool>() == true) {
        logger->info << "setting debug level" << std::flush;
        logger->SetLogLevel("DEBUG");
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();
    config.server_bind_ip = cli_opts["bind_ip"].as<std::string>();
    config.server_port = cli_opts["port"].as<uint16_t>();
    config.server_proto = qtransport::TransportProtocol::QUIC;
    config.transport_config.debug = cli_opts["debug"].as<bool>();
    config.transport_config.tls_cert_filename = const_cast<char *>(cli_opts["cert"].as<std::string>().c_str());
    config.transport_config.tls_key_filename = const_cast<char *>(cli_opts["key"].as<std::string>().c_str());
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.quic_qlog_path = qlog_path.size() ? const_cast<char *>(qlog_path.c_str()) : nullptr;

    return config;
}

int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    auto logger = std::make_shared<cantina::Logger>("qserver");

    cxxopts::Options options("qclient", "MOQ Example Client");
    options
      .set_width(75)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()
      ("h,help", "Print help")
      ("d,debug", "Enable debugging") // a bool parameter
      ("b,bind_ip", "Bind IP", cxxopts::value<std::string>()->default_value("127.0.0.1"))
      ("p,port", "Listening port", cxxopts::value<uint16_t>()->default_value("1234"))
      ("e,endpoint_id", "This relay/server endpoint ID", cxxopts::value<std::string>()->default_value("moq-server"))
      ("c,cert", "Certificate file", cxxopts::value<std::string>()->default_value("./server-cert.pem"))
      ("k,key", "Certificate key file", cxxopts::value<std::string>()->default_value("./server-key.pem"))
      ("q,qlog", "Enable qlog using path", cxxopts::value<std::string>())
    ; // end of options

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help({""}) << std::endl;
        return true;
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock<std::mutex> lock(moq_example::main_mutex);

    quicr::MoQInstanceServerConfig config = init_config(result, logger);

    auto delegate = std::make_shared<serverDelegate>(logger);

    try {
        auto moqInstance = std::make_shared<quicr::MoQInstance>(config, delegate, logger);
        delegate->set_moq_instance(moqInstance);
        moqInstance->run_server();

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
