#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fs_data {
    bool is_host;

    // host data
    void* host_private;

    // client data
    void* client_private;
};
/*
struct priv {
    int fd;
    bool close;
    bool use_poll;
    bool regular_file;
    bool appending;
    int64_t orig_size;
    struct mp_cancel *cancel;
};
*/

int fs_fill_buffer(struct fs_data *s, void *buffer, int max_len);
int fs_seek(struct fs_data *s, int64_t pos);
int64_t fs_get_size(struct fs_data *s);
void fs_close(struct fs_data *s);

void open_stream(struct fs_data *s, bool is_host, char const* url);

#ifdef __cplusplus
}
#endif
