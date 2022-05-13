#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fs_data {
    bool is_host;
    void* private_data;
} fs_data;

typedef struct player_state {
    bool has_ts_update;
    double expected_ts;
    bool has_pause_update;
    bool paused;
} player_state;

int fs_fill_buffer(struct fs_data *s, void *buffer, int max_len);
int fs_seek(struct fs_data *s, int64_t pos);
int64_t fs_get_size(struct fs_data *s);
void fs_close(struct fs_data *s);

void open_stream(struct fs_data *s, bool is_host, char const* url);

void demux_ready(void);
void demux_unready(void);

void notify_demux_status(void);
void wait_for_initial_seek(void);

void register_playloop_wakeup_cb(void(*callback)(void*), void* context);

bool pop_player_state(player_state* ps_out);
bool is_fs_client(void);
bool is_fs_host(void);

void on_host_seek(double pts);
void on_host_pause(bool paused);

#ifdef __cplusplus
}
#endif
