#include "friendstreamer_client.hh"

#include "filecache.hh"
#include "fs_networking.hh"
#include "friendstreamer_internal.hh"

#include <cstring>
#include <condition_variable>

bool has_no_blocking_intervals(std::vector<MPVDataRequest> const& reqs) {
    return std::count_if(reqs.begin(), reqs.end(),
            [] (MPVDataRequest const& req) { return req.type == PendReason::kBlockingRequest; }) == 0;
}

int fs_fill_buffer_client(ClientData& s, void* buffer, int min_len) {
    std::unique_lock lock(s.client_data_m);
    if (s.state == ClientState::kCatchingUp && s.file_loc == s.catchup_start) {
        s.data_wakeup_cv.wait(lock, [&s] { return s.state == ClientState::kContinue; });
    }
    if (s.state == ClientState::kUnconnected) {
        // Block until we're connected
        s.data_wakeup_cv.wait(lock, [&s] { return s.state != ClientState::kUnconnected; });
    }
    min_len = std::min(static_cast<uint64_t>(min_len), s.cache.get_size() - s.file_loc);
    std::optional<Interval> hole =
        s.cache.get_first_missing_interval(Interval(s.file_loc, s.file_loc + min_len));
    const bool is_demux_init = s.state == ClientState::kContinueDemux;
    const bool needed_to_buffer = hole.has_value();
    while (hole) {
        s.requested_intervals =
            std::move(s.cache.get_all_missing_intervals(Interval(hole->left, s.file_loc + min_len)));
        // Notify the network thread that there's a pending data request that needs filling
        s.has_pending_request = true;
        s.reason = PendReason::kBlockingRequest;
        s.network_wakeup_cv.notify_one();

        // Wait until the network thread tells us our request has been filled
        s.data_wakeup_cv.wait(lock, [&s] { return s.requested_intervals.empty() &&
                                                  has_no_blocking_intervals(s.pending_intervals); });
        hole = s.cache.get_first_missing_interval(Interval(s.file_loc, s.file_loc + min_len));
    }

    std::vector<uint8_t> data_read = s.cache.read_data(Interval(s.file_loc, s.file_loc + min_len));
    s.file_loc += data_read.size();
    memcpy(buffer, data_read.data(), data_read.size());

    // In the case that the client was blocked for buffering, notify the network to handle pause-buffer mode
    if (!is_demux_init) {
        if (s.state != ClientState::kCatchingUp &&
            s.state != ClientState::kWaitingForCatchup) {
            if (needed_to_buffer) {
                s.reason = PendReason::kNeedsCatchup;
                s.has_pending_request = true;
                s.network_wakeup_cv.notify_one();
            } else {
                s.requested_intervals =
                    std::move(s.cache.get_all_missing_intervals(Interval(s.file_loc, s.file_loc + kBufferingSize)));
                s.reason = PendReason::kBufferRequest;
                s.has_pending_request = true;
                s.network_wakeup_cv.notify_one();
            }
        }
    } else {
        s.reason = PendReason::kCheckDemuxStatus;
        s.has_pending_request = true;
        s.network_wakeup_cv.notify_one();
    }
    return data_read.size();
}

int fs_seek_client(ClientData& s, int64_t pos) {
    std::unique_lock lock(s.client_data_m);
    if (s.state == ClientState::kUnconnected) {
        // Block until we're connected
        s.data_wakeup_cv.wait(lock, [&s] { return s.state != ClientState::kUnconnected; });
    }
    if (pos >= s.cache.get_size() || pos < 0) {
        return 0;
    } else {
        s.file_loc = pos;
        return 1;
    }
}

int64_t fs_get_size_client(ClientData& s) {
    std::unique_lock lock(s.client_data_m);
    if (s.state == ClientState::kUnconnected) {
        // Block until we're connected
        s.data_wakeup_cv.wait(lock, [&s] { return s.state != ClientState::kUnconnected; });
    }
    return s.cache.get_size();
}

void fs_close_client(ClientData& s) {
    // Yeah, I'll have to deal with this one at some point
    s.cache.clear_cache();
}

static void coalesce_intervals(std::vector<Interval>& iv_list) {
    return;
    if (iv_list.empty()) {
        return;
    }
    for (auto it = iv_list.end() - 1; it != iv_list.begin();) {
        auto it_prev = it - 1;
        for (auto it2 = iv_list.begin(); it2 != it; it2++) {
            if (it->overlaps(*it2)) {
                it2->accrue(*it);
                iv_list.erase(it);
            }
        }
        it = it_prev;
    }
}

static std::vector<Interval> diff_interval(std::vector<MPVDataRequest> const& base, Interval insert) {
    return { insert };
    // std::vector<Interval> ret = { insert };
    // for (auto it = ret.begin(); it != ret.end();) {
    //     bool had_overlap = false;
    //     for (auto it2 = base.begin(); it2 != base.end(); it2++) {
    //         if (it->overlaps(it2->req)) {
    //             if (it->left < it2->req.left) {
    //                 ret.emplace_back(it->left, it2->req.left);
    //             }
    //             if (it->right > it2->req.right) {
    //                 ret.emplace_back(it2->req.right, it->right);
    //             }
    //             ret.erase(it);
    //             had_overlap = true;
    //             break;
    //         }
    //     }
    //     if (!had_overlap) {
    //         it++;
    //     }
    // }
    // return ret;
}

static void send_ready_to_play(ClientData& s) {
    NetworkPacket ready_to_play;
    *ready_to_play.mutable_client_network()->mutable_rtp() = ReadyToPlay();
    send_packet(*s.host_conn, ready_to_play);
}

static void send_buffer_stop(ClientData& s) {
    NetworkPacket buffer_stop;
    buffer_stop.mutable_client_network()->mutable_buffer_stop()->set_stop_timestamp(0);
    send_packet(*s.host_conn, buffer_stop);
}

static bool
handle_data_message(ClientData& s, FileData const& data, std::unique_lock<std::mutex>& lock) {
    auto response_bounds = Interval(data.file_offset(), data.file_offset() + data.raw_data().size());
    auto fulfilled_resp = std::find_if(s.pending_intervals.begin(), s.pending_intervals.end(),
        [response_bounds] (MPVDataRequest iv) {
            return iv.req == response_bounds;
        });
    if (fulfilled_resp == s.pending_intervals.end()) {
        fprintf(stderr, "Warning! unrequested interval filled: %ld-%ld\n", response_bounds.left, response_bounds.right);
        return true;
    }
    s.cache.write_data(data.raw_data().data(), data.raw_data().size(), data.file_offset());
    if (fulfilled_resp->type == PendReason::kBlockingRequest) {
        s.pending_intervals.erase(fulfilled_resp);
        s.data_wakeup_cv.notify_one();
    } else if (fulfilled_resp->type == PendReason::kNeedsCatchup) {
        const auto num_catchup = std::count_if(s.pending_intervals.begin(), s.pending_intervals.end(),
                [] (MPVDataRequest const& req) { return req.type == PendReason::kNeedsCatchup; });
        if (num_catchup == 1) {
            send_ready_to_play(s);
        }
        s.pending_intervals.erase(fulfilled_resp);
    }
    return true;
}

static size_t
request_for_interval(ClientData& s, Interval new_iv, PendReason reason) {
    std::vector<Interval> holes = s.cache.get_all_missing_intervals(new_iv);
    std::vector<Interval> holes_diff;
    for (Interval req : holes) {
        std::vector<Interval> diff = diff_interval(s.pending_intervals, req);
        holes_diff.insert(holes_diff.end(), diff.begin(), diff.end());
    }
    
    for (Interval req : holes_diff) {
        NetworkPacket data_req;
        data_req.mutable_client_network()->mutable_data_req()->set_file_off(req.left);
        data_req.mutable_client_network()->mutable_data_req()->set_length(req.right - req.left);
        send_packet(*s.host_conn, data_req);
        s.pending_intervals.push_back({req, reason});
    }
    return holes_diff.size();
}

static void
handle_control_message(ClientData& s, ControlMessage const& cm) {
    if (cm.has_pause()) {
        PauseMessage const& pm = cm.pause();
        if (pm.pause() && !pm.soft_pause()) {
            set_player_state_ts(pm.sync_timestamp());
            const size_t num_reqs_made =
                request_for_interval(s,
                        Interval(pm.sync_filepos(), pm.sync_filepos() + kGoodToGoSize),
                        PendReason::kNeedsCatchup);
            if (num_reqs_made == 0) {
                send_ready_to_play(s);
            }
            // We're the reason for this pause, so let MPV know we've corrected our playback and seek some new data
            if (s.state == ClientState::kWaitingForCatchup) {
                s.state = ClientState::kCatchingUp;
                s.data_wakeup_cv.notify_one();
            }
            s.state = ClientState::kCatchingUp;
        } else if (pm.pause() && pm.soft_pause()) {
            set_player_state_pause(true);
        } else { // Unpause
            if (s.state == ClientState::kCatchingUp) {
                s.state = ClientState::kContinue;
                s.data_wakeup_cv.notify_one();
            }
            set_player_state_pause(false);
        }
    } else if (cm.has_seek()) {
        SeekMessage const& sm = cm.seek();
        set_player_state_ts(sm.sync_timestamp());
        const size_t num_reqs_made =
            request_for_interval(s,
                    Interval(sm.sync_filepos(), sm.sync_filepos() + kGoodToGoSize),
                    PendReason::kNeedsCatchup);
        if (num_reqs_made == 0) {
            send_ready_to_play(s);
        }
        s.state = ClientState::kCatchingUp;
    }
}

static std::pair<double, uint64_t> client_handshake(ClientData* data) {
    // Receive Handshake initialize message (file size) and wait for mpv to read
    auto pkt_opt_init = recv_packet(*data->host_conn);
    if (!pkt_opt_init->has_hs()) {
        perror("Received unexpected message from host during handshake init, expected hs\n");
        return std::make_pair(0.0, -1);
    }
    if (!pkt_opt_init->hs().has_init()) {
        perror("Received unexpected message from host during handshake init, expected init\n");
        return std::make_pair(0.0, -1);
    }
    std::unique_lock lock(data->client_data_m);
    data->cache.create_cache(kStreambufName, pkt_opt_init->hs().init().file_size());
    data->state = ClientState::kContinueDemux;
    data->data_wakeup_cv.notify_one();

    // Begin requesting and receiving of data chunks until demuxer is chosen
    while (!demuxer_good()) {
        data->network_wakeup_cv.wait(lock, [&data] { return data->has_pending_request; });

        std::vector<Interval> pending_fills = std::move(data->requested_intervals);
        data->has_pending_request = false;
        for (auto const& iv : pending_fills) {
            NetworkPacket data_req;
            data_req.mutable_hs()->mutable_phase2()->set_status(HSPhase2_ParseStatus_kMore);
            data_req.mutable_hs()->mutable_phase2()->mutable_request()->set_file_off(iv.left);
            data_req.mutable_hs()->mutable_phase2()->mutable_request()->set_length(iv.right - iv.left);
            send_packet(*data->host_conn, data_req);
            data->pending_intervals.push_back({iv, PendReason::kBlockingRequest});
        }
        // Demux choose phase should never-ever receive have buffer_block be true, since no video is playing
        // For my own sanity, I'll assume the host will send back an equal number of responses to reqs
        // not coalescing things. If I decide to get smart later, shoot me
        while (!data->pending_intervals.empty()) {
            auto pkt_opt = recv_packet(*data->host_conn);
            if (!pkt_opt->has_hs()) {
                perror("Received unexpected message from host during demux choose, expected hs\n");
                return std::make_pair(0.0, -1);
            }
            if (!pkt_opt->hs().has_phase1()) {
                perror("Received unexpected message from host during demux choose, expected phase2\n");
                return std::make_pair(0.0, -1);
            }
            FileData const& hs_chunk = pkt_opt->hs().phase1().file_chunk();
            data->cache.write_data(hs_chunk.raw_data().data(),
                                   hs_chunk.raw_data().size(),
                                   hs_chunk.file_offset());
            auto found_iv = std::find_if(data->pending_intervals.begin(), data->pending_intervals.end(),
                [&hs_chunk] (MPVDataRequest const& data_req) {
                    return data_req.req ==
                           Interval(hs_chunk.file_offset(), hs_chunk.file_offset() + hs_chunk.raw_data().size());
                });
            if (found_iv == data->pending_intervals.end()) {
                perror("Received unexpected region!");
                return std::make_pair(0.0, -1);
            }
            data->pending_intervals.erase(found_iv);
        }
        data->data_wakeup_cv.notify_one();
        data->network_wakeup_cv.wait(lock, [&data] { return data->has_pending_request; });
        data->has_pending_request = false;
        if (data->reason != PendReason::kCheckDemuxStatus) {
            perror("Unexpected pend reason during demux choose\n");
            return std::make_pair(0.0, -1);
        }

        lock.unlock();
        block_for_demuxer();
        lock.lock();
    }
    NetworkPacket response;
    response.mutable_hs()->mutable_phase2()->set_status(HSPhase2_ParseStatus_kGood);
    send_packet(*data->host_conn, response);
    auto pkt_opt = recv_packet(*data->host_conn);
    if (!pkt_opt->has_hs()) {
        perror("Received unexpected message from host after demux choose, expected hs\n");
        return std::make_pair(0.0, -1);
    }
    if (!pkt_opt->hs().has_phase3()) {
        perror("Received unexpected message from host after demux choose, expected phase3\n");
        return std::make_pair(0.0, -1);
    }
    data->state = ClientState::kContinue;
    return std::make_pair(pkt_opt->hs().phase3().start_point().sync_timestamp(),
                          pkt_opt->hs().phase3().start_point().sync_filepos());
}

void client_network(ClientData* data) {
    auto&& [stream_start_ts, stream_start_fp] = client_handshake(data);
    if (stream_start_fp == -1) {
        perror("Failed to handshake with host!\n");
        exit(1);
    }
    set_player_state_ts(stream_start_ts);
    notify_initial_seek();

    // nonblocking not needed?
    asio::error_code ec;
    data->host_conn->skt.non_blocking(true, ec);

    std::unique_lock lock(data->client_data_m);
    while (true) {
        data->network_wakeup_cv.wait_for(lock, std::chrono::milliseconds(16),
            [&data] { return data->has_pending_request; });

        if (data->has_pending_request) {
            if (data->reason == PendReason::kBufferRequest ||
                data->reason == PendReason::kBlockingRequest) {
                // We need to mangle the incoming request intervals, since they could overlap with
                // both the filecache (unlikely), or pending responses (more likely)
                std::vector<Interval> new_requests, new_requests_diff;
                for (Interval const& req : data->requested_intervals) {
                    std::vector<Interval> holes = data->cache.get_all_missing_intervals(req);
                    new_requests.insert(new_requests.end(), holes.begin(), holes.end());
                }
                coalesce_intervals(new_requests);
                for (Interval const& req : new_requests) {
                    std::vector<Interval> diffed_req = diff_interval(data->pending_intervals, req);
                    new_requests_diff.insert(new_requests_diff.end(), diffed_req.begin(), diffed_req.end());
                }

                for (Interval const& req : new_requests_diff) {
                    NetworkPacket data_req;
                    data_req.mutable_client_network()->mutable_data_req()->set_file_off(req.left);
                    data_req.mutable_client_network()->mutable_data_req()->set_length(req.right - req.left);
                    send_packet(*data->host_conn, data_req);
                    data->pending_intervals.push_back({req, data->reason});
                }
                data->requested_intervals.clear();
            } else if (data->reason == PendReason::kNeedsCatchup) {
                data->state = ClientState::kWaitingForCatchup;
                send_buffer_stop(*data);
            }
        }
        data->has_pending_request = false;

        for (;;) {
            auto pkt_opt = recv_packet(*data->host_conn);
            if (!pkt_opt) {
                break;
            }
            if (!pkt_opt->has_host_network()) {
                continue;
            }
            HostNetwork const& host_msg = pkt_opt->host_network();
            if (host_msg.has_control()) {
                handle_control_message(*data, host_msg.control());
            } else if (host_msg.has_data()) {
                if (!handle_data_message(*data, host_msg.data(), lock)) {
                    break;
                }
            } else if (host_msg.has_hb()) {
                // TODO: make this work
            }
        }
    }
}

ClientData* create_client(char const* url) {
    auto new_conn = connect_to(std::make_shared<asio::io_service>(), url);

    ClientData* new_data = new ClientData();
    new_data->file_loc = 0;
    new_data->host_conn = std::make_unique<AsioSocket>(std::move(new_conn));
    new_data->network_thread = std::make_unique<std::thread>(client_network, new_data);
    new_data->has_pending_request = false;
    return new_data;
}
