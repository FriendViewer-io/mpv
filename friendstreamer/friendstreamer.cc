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
static player_state s_client_player_state = {
    .has_ts_update = false,
    .expected_ts = 0,
    .has_pause_update = false,
    .paused = false,
};


static std::atomic<bool> s_is_ready_and_host = false;
static std::mutex s_host_player_state_m;
static player_state s_host_player_state = {
    .has_ts_update = false,
    .expected_ts = 0,
    .has_pause_update = false,
    .paused = false,
};
static std::atomic<double> s_active_host_ts;

extern "C" {
int fs_fill_buffer(struct fs_data *s, void *buffer, int max_len) {
    if (s->is_host) {
        return 0;
    }
    return fs_fill_buffer_client(*reinterpret_cast<ClientData*>(s->private_data), buffer, max_len); 
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
    printf("Demux ready!\n");
}

void notify_demux_status() {
    {
        std::lock_guard guard(s_client_demux_m);
        s_client_demux_wake = true;
        printf("Notified FS for demux state!\n");
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
    if (!s_client_player_state.has_pause_update && !s_client_player_state.has_ts_update) {
        return false;
    }
    *ps_out = s_client_player_state;
    s_client_player_state.has_ts_update = s_client_player_state.has_pause_update = false;
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
    s_host_player_state.has_ts_update = true;
    s_host_player_state.expected_ts =  demux_pts;
}

void on_host_pause(bool paused) {
    std::lock_guard guard(s_host_player_state_m);
    s_host_player_state.has_pause_update = true;
    s_host_player_state.paused = paused;
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
    s_client_player_state.has_pause_update = true;
    s_client_player_state.paused = paused;
}

void set_player_state_ts(double ts) {
    std::lock_guard guard(s_client_player_state_m);
    s_client_player_state.has_ts_update = true;
    s_client_player_state.expected_ts = ts;
}

void notify_initial_seek() {
    std::lock_guard guard(s_client_demux_m);
    s_did_initial_seek = true;
    s_client_demux_cv.notify_one();
}
