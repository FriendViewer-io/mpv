#pragma once

#include "filecache.hh"
#include "fs_networking.hh"

#include <condition_variable>
#include <mutex>
#include <thread>

enum class ClientState : int {
    // Client has yet to connect, cache is uninitialized
    kUnconnected,
    // Client is waiting for the host to give a pause message
    kWaitingForCatchup,
    // Client (should) be paused, waiting to make sure we've got coverage
    kCatchingUp,
    // Client is attempting to continue as normal,
    kContinue,

    // Same as the above two, but the demuxer is being chosen right now
    // implying that we're handshaking the host
    kContinueDemux,
};

// With a mid-quality mkv (h264 + ogg), it seems that 64k stream reads were done about
// once per second-ish, so this should be a good amount to back us up
constexpr size_t kBufferingSize = 65536 * 10;
// We'll tell the host we're good to go once we have at least this many bytes buffered
// ahead of us
constexpr size_t kGoodToGoSize = 65536 * 4;

enum class PendReason {
    kBufferRequest,
    kBlockingRequest,
    kNeedsCatchup,
    kCheckDemuxStatus,
    kWaitForUnpause,
};

struct MPVDataRequest {
    Interval req;
    PendReason type;
};

// To give situations where network constraints make the above buffering params impossible,
// these parameters should self-tune (make up some clever heuristic)

struct ClientData {
    std::unique_ptr<std::thread> network_thread;
    std::mutex client_data_m;
    std::condition_variable data_wakeup_cv;
    ClientState state;

    // Read by mpv, written by network
    FileCache cache;
    int64_t file_loc;
    int64_t catchup_start;
    std::unique_ptr<AsioSocket> host_conn;
    std::vector<MPVDataRequest> pending_intervals;

    // Read by network, written by mpv
    std::condition_variable network_wakeup_cv;
    std::vector<Interval> requested_intervals;
    bool has_pending_request = false;
    PendReason reason;

    // Tertiary
    std::vector<Interval> catchup_intervals;
};

int fs_fill_buffer_client(ClientData& s, void* buffer, int min_len);
int fs_seek_client(ClientData& s, int64_t pos);
int64_t fs_get_size_client(ClientData& s);
void fs_close_client(ClientData& s);

ClientData* create_client(char const* url);
