#pragma once

#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <asio/ip/tcp.hpp>

#include "fs_networking.hh"

struct HostData {
    std::vector<AsioSocket> clients;
    std::unique_ptr<std::thread> acceptor_thread;
    std::unique_ptr<std::thread> network_thread;
    std::ifstream streamed_file_handle;
    std::mutex host_data_m;
    std::shared_ptr<asio::io_service> io_svc;
};


HostData* create_host(char const* url);
