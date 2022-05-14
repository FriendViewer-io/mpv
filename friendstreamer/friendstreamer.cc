#include "friendstreamer.hh"
#include "friendstreamer_internal.hh"

#include "friendstreamer_client.hh"
#include "friendstreamer_host.hh"

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>

#include <asio/io_service.hpp>
#include <asio/ip/tcp.hpp>

static bool s_demux_ready = false;
static std::condition_variable s_client_demux_cv;
static std::mutex s_client_demux_m;
static bool s_client_demux_wake = false;
static bool s_did_initial_seek = false;
static std::atomic<bool> s_is_ready_and_client = false;
static void* mpctx_ptr = nullptr;
static void(*wakeup_playloop_cb)(void*) = nullptr;
static std::mutex s_client_player_state_m;
static player_state s_player_state_change = {
    .has_ts_update = false,
    .expected_ts = 0,
    .expected_file_pos = 0,
    .has_pause_update = false,
    .paused = false,
};


static std::atomic<bool> s_is_ready_and_host = false;
static std::mutex s_host_player_state_m;
static player_state s_host_player_state = {
    .has_ts_update = false,
    .expected_ts = 0,
    .expected_file_pos = 0,
    .has_pause_update = false,
    .paused = false,
};
static std::atomic<double> s_active_host_ts;
static double s_last_pts = 0;
static uint64_t s_last_fp = 0;

extern "C" {
int fs_fill_buffer(struct fs_data *s, void *buffer, int max_len) {
    if (s->is_host) {
        return 0;
    }
    int val = fs_fill_buffer_client(*reinterpret_cast<ClientData*>(s->private_data), buffer, max_len); 
    return val;
}

int fs_seek(struct fs_data *s, int64_t pos) {
    if (s->is_host) {
        return 0;
    }
    return fs_seek_client(*reinterpret_cast<ClientData*>(s->private_data), pos);
}

int64_t fs_get_size(struct fs_data *s) {
    if (s->is_host) {
        return 0;
    }
    return fs_get_size_client(*reinterpret_cast<ClientData*>(s->private_data));
}

void fs_close(struct fs_data *s) {
    if (s->is_host) {
        return;
    }
    fs_close_client(*reinterpret_cast<ClientData*>(s->private_data));
}

void open_stream(struct fs_data *s, bool is_host, char const* url) {
    s->is_host = is_host;
    if (is_host) {
        s->private_data = create_host(url);
        set_is_host();
    } else {
        s->private_data = create_client(url);
        set_is_client();
    }
}

void demux_unready() {
    std::lock_guard guard(s_client_demux_m);
    s_demux_ready = false;
}

void demux_ready() {
    std::lock_guard guard(s_client_demux_m);
    s_demux_ready = true;
}

void notify_demux_status() {
    {
        std::lock_guard guard(s_client_demux_m);
        s_client_demux_wake = true;
    }
    s_client_demux_cv.notify_one();
}

void wait_for_initial_seek() {
    std::unique_lock lock(s_client_demux_m);
    s_client_demux_cv.wait(lock, [] { return s_did_initial_seek; });
}

void register_playloop_wakeup_cb(void(*callback)(void*), void* context) {
    wakeup_playloop_cb = callback;
    mpctx_ptr = context;
}

bool pop_player_state(player_state* ps_out) {
    std::lock_guard guard(s_client_player_state_m);
    if (!s_player_state_change.has_pause_update && !s_player_state_change.has_ts_update) {
        return false;
    }
    *ps_out = s_player_state_change;
    s_player_state_change.has_ts_update = s_player_state_change.has_pause_update = false;
    return true;
}

bool is_fs_client() {
    return s_is_ready_and_client.load();
}

bool is_fs_host() {
    return s_is_ready_and_host.load();
}

void on_host_seek(double demux_pts) {
    std::lock_guard guard(s_host_player_state_m);
    s_host_player_state.expected_ts = demux_pts;
}

void on_host_seek_file(uint64_t file_pos) {
    std::lock_guard guard(s_host_player_state_m);
    s_host_player_state.has_ts_update = true;
    s_host_player_state.expected_file_pos = file_pos;
}

void on_host_pause(bool paused) {
    std::lock_guard guard(s_host_player_state_m);
    s_host_player_state.has_pause_update = true;
    s_host_player_state.paused = paused;
}

void set_buffer_fill_point(uint64_t fp) {
    std::lock_guard guard(s_host_player_state_m);
    s_last_fp = fp;
}

void set_timestamp(double ts) {
    std::lock_guard guard(s_host_player_state_m);
    s_last_pts = ts;
}
}

bool demuxer_good() {
    std::lock_guard guard(s_client_demux_m);
    return s_demux_ready;
}

void block_for_demuxer() {
    std::unique_lock block(s_client_demux_m);
    s_client_demux_cv.wait(block, [] { return s_client_demux_wake; });
    s_client_demux_wake = false;
}

void wakeup_playloop() {
    if (wakeup_playloop_cb != nullptr) {
        wakeup_playloop_cb(mpctx_ptr);
    }
}

void set_is_client() {
    s_is_ready_and_client.store(true);
}

void set_is_host() {
    s_is_ready_and_host.store(true);
}

void set_player_state_pause(bool paused) {
    std::lock_guard guard(s_client_player_state_m);
    s_player_state_change.has_pause_update = true;
    s_player_state_change.paused = paused;
    wakeup_playloop();
}

void set_player_state_ts(double ts) {
    std::lock_guard guard(s_client_player_state_m);
    s_player_state_change.has_ts_update = true;
    s_player_state_change.expected_ts = ts;
    wakeup_playloop();
}

void notify_initial_seek() {
    std::lock_guard guard(s_client_demux_m);
    s_did_initial_seek = true;
    s_client_demux_cv.notify_one();
}

bool pop_host_state(player_state* ps_out) {
    std::lock_guard guard(s_host_player_state_m);
    if (!s_host_player_state.has_pause_update && !s_host_player_state.has_ts_update) {
        return false;
    }
    *ps_out = s_host_player_state;
    s_host_player_state.has_ts_update = s_host_player_state.has_pause_update = false;
    return true;
}

std::pair<double, uint64_t> get_last_fill_point() {
    std::lock_guard guard(s_host_player_state_m);
    return std::make_pair(s_last_pts, s_last_fp);
}
