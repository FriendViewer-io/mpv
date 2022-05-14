#pragma once

#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <asio/ip/tcp.hpp>

#include "fs_networking.hh"

enum class ClientTrackerState {
    kInitial,
    kHandshaking,
    kNormal,
};

struct ClientTracker {
    AsioSocket conn;
    ClientTrackerState state;

    ClientTracker(AsioSocket&& skt)
        : conn(std::move(skt)), state(ClientTrackerState::kInitial) {}
};

enum class HostState {
    kPlaying,
    kPaused,
    kWaitingForReady,
};

struct HostData {
    std::mutex host_data_m;
    HostState state = HostState::kPlaying;
    size_t file_size;
    std::vector<ClientTracker> clients;
    std::unique_ptr<std::thread> acceptor_thread;
    std::unique_ptr<std::thread> network_thread;
    std::ifstream streamed_file_handle;
    std::shared_ptr<asio::io_service> io_svc;
};


HostData* create_host(char const* url);
