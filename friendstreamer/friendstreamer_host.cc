#include "friendstreamer_host.hh"

#include "friendstreamer.hh"
#include "friendstreamer_internal.hh"
#include "proto-generated/friendstreamer.pb.h"

#include <string>

using namespace asio::ip;

void host_accept(HostData* data) {
    while (true) {
        AsioSocket new_conn = await_connection(data->io_svc);
        asio::error_code ec;
        new_conn.skt.non_blocking(true, ec);
        {
            std::lock_guard<std::mutex> guard(data->host_data_m);
            data->clients.emplace_back(std::move(new_conn));
        }
    }
}

void handle_client_locked(HostData& data, ClientTracker& client, std::optional<player_state> const& player_state) {
    if (client.state == ClientTrackerState::kInitial) {
        NetworkPacket initial_hs;
        initial_hs.mutable_hs()->mutable_init()->set_file_size(data.file_size);
        send_packet(client.conn, initial_hs);
        client.state = ClientTrackerState::kHandshaking;
    } else if (client.state == ClientTrackerState::kHandshaking) {
        std::optional<NetworkPacket> pkt = recv_packet(client.conn);
        if (!pkt) {
            return;
        }
        if (!pkt->has_hs()) {
            return;
        }
        if (pkt->hs().has_phase2()) {
            NetworkPacket data_resp;
            HSPhase2 const& p2 = pkt->hs().phase2();
            if (p2.status() == HSPhase2_ParseStatus_kMore) {
                FileData* fd = data_resp.mutable_hs()->mutable_phase1()->mutable_file_chunk();
                std::string* data_out = fd->mutable_raw_data();
                data.streamed_file_handle.seekg(p2.request().file_off());
                data_out->resize(p2.request().length());
                data.streamed_file_handle.read(data_out->data(), data_out->size());
                fd->set_file_offset(p2.request().file_off());
            } else if (p2.status() == HSPhase2_ParseStatus_kGood) {
                auto&& [last_pts, last_fp] = get_last_fill_point();
                data_resp.mutable_hs()->mutable_phase3()->mutable_start_point()->set_sync_filepos(last_fp);
                data_resp.mutable_hs()->mutable_phase3()->mutable_start_point()->set_sync_timestamp(last_pts);
                client.state = ClientTrackerState::kNormal;
            }
            send_packet(client.conn, data_resp);
        }
    } else {
        if (player_state && player_state->has_ts_update) {
            NetworkPacket resp_seek;
            resp_seek.mutable_host_network()->mutable_control()->mutable_seek()->set_sync_timestamp(player_state->expected_ts);
            resp_seek.mutable_host_network()->mutable_control()->mutable_seek()->set_sync_filepos(player_state->expected_file_pos);
            set_player_state_pause(false);
            send_packet(client.conn, resp_seek);
        } else if (player_state && player_state->has_pause_update) {
            NetworkPacket resp_pause;
            resp_pause.mutable_host_network()->mutable_control()->mutable_pause()->set_soft_pause(true);
            resp_pause.mutable_host_network()->mutable_control()->mutable_pause()->set_pause(true);
            send_packet(client.conn, resp_pause);
        }
        std::optional<NetworkPacket> pkt = recv_packet(client.conn);
        if (!pkt) {
            return;
        }
        if (!pkt->has_client_network()) {
            return;
        }
        NetworkPacket resp;
        if (pkt->client_network().has_data_req()) {
            DataRequest const& dr = pkt->client_network().data_req();
            FileData* fd = resp.mutable_host_network()->mutable_data();
            std::string* data_out = fd->mutable_raw_data();
            data.streamed_file_handle.seekg(dr.file_off());
            data_out->resize(dr.length());
            data.streamed_file_handle.read(data_out->data(), data_out->size());
            fd->set_file_offset(dr.file_off());
            send_packet(client.conn, resp);
        } else if (pkt->client_network().has_buffer_stop()) {
            resp.mutable_host_network()->mutable_control()->mutable_pause()->set_pause(true);
            resp.mutable_host_network()->mutable_control()->mutable_pause()->set_soft_pause(false);
            auto&& [last_ts, last_fp] = get_last_fill_point();
            resp.mutable_host_network()->mutable_control()->mutable_pause()->set_sync_timestamp(last_ts);
            resp.mutable_host_network()->mutable_control()->mutable_pause()->set_sync_filepos(last_fp);
            send_packet(client.conn, resp);
            set_player_state_pause(true);
        } else if (pkt->client_network().has_rtp()) {
            set_player_state_pause(false);
            resp.mutable_host_network()->mutable_control()->mutable_pause()->set_pause(false);
            resp.mutable_host_network()->mutable_control()->mutable_pause()->set_soft_pause(false);
            send_packet(client.conn, resp);
        }
    }
}

void host_network(HostData* data) {
    while (true) {
        player_state host_state;
        std::optional<player_state> ps = std::nullopt;
        if (pop_host_state(&host_state)) {
            if (host_state.has_ts_update) {
                NetworkPacket timestamp;
            }
            if (host_state.has_pause_update) {
            }
            ps = host_state;
        }
        {
            std::lock_guard<std::mutex> guard(data->host_data_m);
            for (auto&& client : data->clients) {
                handle_client_locked(*data, client, ps);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

HostData* create_host(char const* url) {
    char const* path = url + sizeof("fshost://") - 1;
    auto file_handle = std::ifstream(path, std::ios::binary);
    if (!file_handle.is_open()) {
        return nullptr;
    }
    HostData* new_host = new HostData();
    file_handle.seekg(0, std::ios::end);
    new_host->file_size = file_handle.tellg();
    new_host->io_svc = std::make_shared<asio::io_service>();
    new_host->streamed_file_handle = std::move(file_handle);
    new_host->acceptor_thread = std::make_unique<std::thread>(host_accept, new_host);
    new_host->network_thread = std::make_unique<std::thread>(host_network, new_host);
    return new_host;
}
