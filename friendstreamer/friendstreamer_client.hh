#pragma once
#include "filecache.hh"`fi

struct ClientData {
    FileCache cache;
    int64_t file_loc;
};

int fs_fill_buffer_client(ClientData& s, void* buffer, int min_len, int max_len);
int fs_seek_client(ClientData& s, int64_t pos);
int64_t fs_get_size_client(ClientData& s);
void fs_close_client(ClientData& s);