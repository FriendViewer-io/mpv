#include "friendstreamer_host.hh"

#include "proto-generated/friendstreamer.pb.h"

using namespace asio::ip;

void host_accept(HostData* data) {
    while (true) {
        AsioSocket new_conn = await_connection(data->io_svc);
        asio::error_code ec;
        new_conn.skt.native_non_blocking(true, ec);
        {
            std::lock_guard<std::mutex> guard(data->host_data_m);
            data->clients.emplace_back(std::move(new_conn));
        }
    }
}

void host_network(HostData* data) {
    while (true) {
        std::lock_guard<std::mutex> guard(data->host_data_m);
        for (auto&& client : data->clients) {
            std::optional<NetworkPacket> pkt = recv_packet(client);
        }
    }
}

HostData* create_host(char const* url) {
    char const* path = url + sizeof("fshost://") - 1;
    auto file_handle = std::ifstream(path, std::ios::binary);
    if (!file_handle.is_open()) {
        return nullptr;
    }
    HostData* new_host = new HostData();
    new_host->io_svc = std::make_shared<asio::io_service>();
    new_host->streamed_file_handle = std::move(file_handle);
    new_host->acceptor_thread = std::make_unique<std::thread>(host_accept, new_host);
    new_host->network_thread = std::make_unique<std::thread>(host_network, new_host);
    return new_host;
}
