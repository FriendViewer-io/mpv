#include "friendstreamer.hh"

#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <asio/io_service.hpp>
#include <asio/ip/tcp.hpp>

constexpr asio::ip::port_type kPortNum = 40040;

enum class PacketType : uint8_t {
    UNKNOWN,
    DATA,
    CONTROL
};

enum class ControlMessageType : uint8_t {
    SKIP,
    PAUSE,
    PLAY
};

struct packet {
    PacketType packet_type;
    uint32_t len;
};

struct data_packet : packet {
    uint32_t offset;
    uint32_t size;
    void *data;
};

struct control_packet : packet {
    ControlMessageType control_type;
    uint32_t skip_pos;
};

struct asio_socket { 
    asio_socket() : skt(io_svc) {}
    asio_socket(asio_socket&&) = delete;
    asio_socket(asio_socket const&) = delete;

    asio::io_service io_svc;
    asio::ip::tcp::socket skt;
};

struct host_data {
    std::vector<asio_socket> clients;
    std::thread* acceptor_thread;
    std::thread* network_thread;
    std::ifstream file_handle;
    std::mutex data_m;
};

struct client_data {
    asio::ip::tcp::socket host;
    std::thread* network_thread;
    std::fstream buffer_handle;
};

void client_network_thread(client_data* data) {
    std::string recv_buffer;
    recv_buffer.resize(1500);
    asio::error_code ec;
    packet pkt = {PacketType::CONTROL, 0};
    while (true) {
        size_t recv_size = data->host.receive(asio::buffer(recv_buffer), 0, ec);
        if (recv_size == 0 || ec.value() != 0) {
            break;
        }
        char const* data = recv_buffer.data();
        if (pkt.packet_type == PacketType::UNKNOWN) {
            auto recv_pkt = reinterpret_cast<packet const*>(data);
            pkt.packet_type = recv_pkt->packet_type;
            pkt.len = recv_pkt->len;
            data += sizeof(packet);
        }
    }
}

void host_network_thread(host_data* data) {
    std::string recv_buffer;
    recv_buffer.resize(1500);
    while (true) {
        for (auto&& client : data->clients) {
            asio::error_code ec;
            size_t recv_size = client.skt.receive(asio::buffer(recv_buffer, 1500), 0, ec);
            if (recv_size == 0 || ec.value() != 0) {
                break;
            }
        }
    }
}

void host_acceptor_thread(host_data* data) {
    while (true) {
        asio_socket new_conn;
        asio::ip::tcp::acceptor acceptor(new_conn.io_svc, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), kPortNum));
        asio::socket_base::reuse_address reuse(true);
        acceptor.set_option(reuse);
        acceptor.accept(new_conn.skt);
        {
            // Lock our host_data structure and add a new non-blocking connection to the client
            std::lock_guard<std::mutex> lck(data->data_m);
            asio::error_code ec;
            new_conn.skt.native_non_blocking(true, ec);
            data->clients.emplace_back(std::move(new_conn));
        }
    }
}

extern "C" {
int fs_fill_buffer(struct fs_data *s, void *buffer, int max_len) {
    
}

int fs_seek(struct fs_data *s, int64_t pos) {

}

int64_t fs_get_size(struct fs_data *s) {

}

void fs_close(struct fs_data *s) {

}

void open_stream(struct fs_data *s, bool is_host, char const* url) {
    s->is_host = is_host;
    if (is_host) {
        host_data* new_host_data = new host_data;
        new_host_data->file_handle = std::ifstream(url);
        s->host_private = new_host_data;
        new_host_data->acceptor_thread = new std::thread(host_acceptor_thread, s);
        new_host_data->network_thread = new std::thread(host_network_thread, s);
    } else {
        // TODO: Decode URL to determine host
        client_data* new_client_data = new client_data;
        new_client_data->buffer_handle = std::fstream(".streambuf", std::ios::trunc | std::ios::binary | std::ios::in | std::ios::out);
        s->client_private = new_client_data;
        new_client_data->network_thread = new std::thread(client_network_thread, s);
    }
}

}
