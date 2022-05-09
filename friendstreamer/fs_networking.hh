#pragma once

#include <asio/io_service.hpp>
#include <asio/ip/tcp.hpp>
#include <memory>
#include <optional>
#include <vector>

#include "proto-generated/friendstreamer.pb.h"

using tcp_socket = asio::ip::tcp::socket;
using tcp_acceptor = asio::ip::tcp::acceptor;

constexpr static asio::ip::port_type kPortNum = 40040;

struct AsioSocket {
    AsioSocket(std::shared_ptr<asio::io_service> io_svc);
    AsioSocket(AsioSocket&& rhs) = default;
    AsioSocket(AsioSocket const&) = delete;

    AsioSocket& operator=(AsioSocket&&) = default;
    AsioSocket& operator=(AsioSocket const&) = delete;

    void send_msg(void const* data, size_t length);
    std::optional<std::vector<uint8_t>> recv_msg();

    ~AsioSocket();

    std::shared_ptr<asio::io_service> io_svc;
    tcp_socket skt;

private:
    enum class ReadMode {
        kReadLength, kReadData
    };
    std::vector<uint8_t> pending_read;
    ReadMode read_mode;
};

AsioSocket await_connection(std::shared_ptr<asio::io_service> io_svc);
std::optional<NetworkPacket> recv_packet(AsioSocket& skt);
void send_packet(AsioSocket& skt, NetworkPacket const& pkt);
