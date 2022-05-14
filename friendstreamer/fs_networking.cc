#include "fs_networking.hh"

#include <asio.hpp>
#include <asio/ip/tcp.hpp>

using namespace asio::ip;

AsioSocket::AsioSocket(std::shared_ptr<asio::io_service> io_svc) 
    : io_svc(std::move(io_svc)), skt(*this->io_svc.get())
{
    pending_read.resize(sizeof(size_t));
    read_mode = ReadMode::kReadLength;
    current_read_amount = 0;
}

void AsioSocket::send_msg(void const* data, size_t length) {
    asio::error_code ec;
    asio::write(skt, asio::buffer(&length, sizeof(length)), ec);
    asio::write(skt, asio::buffer(data, length), ec);
}

void AsioSocket::partial_recv() {
    asio::error_code ec;
    const size_t read_sz = asio::read(skt, asio::buffer(pending_read.data() + current_read_amount, pending_read.size()-current_read_amount), ec);
    current_read_amount += read_sz;
}

std::optional<std::vector<uint8_t>> AsioSocket::recv_msg() {
    if (read_mode == ReadMode::kReadLength) {
        partial_recv();
        if (current_read_amount == pending_read.size()) {
            size_t len = *reinterpret_cast<size_t*>(pending_read.data());
            pending_read.resize(len);
            current_read_amount = 0;
            read_mode = ReadMode::kReadData;
        } else {
            return std::nullopt;
        }
    }
    if (read_mode == ReadMode::kReadData) {
        partial_recv();
        if (current_read_amount == pending_read.size()) {
            std::vector<uint8_t> ret = std::move(pending_read);
            pending_read.resize(sizeof(size_t));
            read_mode = ReadMode::kReadLength;
            current_read_amount = 0;
            return ret;
        } else {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

AsioSocket::~AsioSocket() {
    skt.close();
}

AsioSocket await_connection(std::shared_ptr<asio::io_service> io_svc) {
    auto new_conn = AsioSocket(io_svc);
    auto acceptor = tcp::acceptor(*io_svc, tcp::endpoint(tcp::v4(), kPortNum));
    asio::socket_base::reuse_address reuse(true);
    acceptor.set_option(reuse);
    acceptor.accept(new_conn.skt);
    acceptor.close();
    return new_conn;
}

AsioSocket connect_to(std::shared_ptr<asio::io_service> io_svc, char const* url) {
    asio::error_code ec;
    auto new_conn = AsioSocket(io_svc);
    char const* ip = url + sizeof("fsclient://") - 1;
    auto endpoint = tcp::endpoint(address::from_string(ip), kPortNum);
    new_conn.skt.connect(endpoint, ec);
    return new_conn;
}

std::optional<NetworkPacket> recv_packet(AsioSocket& skt) {
    auto pkt_raw = skt.recv_msg();
    if (!pkt_raw) {
        return std::nullopt;
    }
    NetworkPacket pkt;
    pkt.ParseFromArray(pkt_raw->data(),pkt_raw->size());
    return pkt;
}

void send_packet(AsioSocket& skt, NetworkPacket const& pkt) {
    std::string pkt_raw = pkt.SerializeAsString();
    skt.send_msg(pkt_raw.data(), pkt_raw.size());
}
