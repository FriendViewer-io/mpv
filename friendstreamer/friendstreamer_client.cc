#include "friendstreamer_client.hh"
#include "filecache.hh"

int fs_fill_buffer_client(ClientData& s, void* buffer, int min_len, int max_len) {
    std::optional<Interval> hole = s.cache.get_first_missing_interval(Interval(s.file_loc,s.file_loc + max_len));

    // If we get nullopt, that means we can use the interval file_loc, file_loc + max_len.
    if (hole == std::nullopt) {
        std::vector<uint8_t> data_read = s.cache.read_data(Interval(s.file_loc, s.file_loc + max_len));
        memcpy(buffer, data_read.data(), data_read.size());
        return data_read.size();

    // If we find a hole and that hole's .left is greater than (or equal to) file_loc + min, then we use the interval file_loc, hole->left
    } else if (hole->left >= s.file_loc + min_len) {
        std::vector<uint8_t> data_read = s.cache.read_data(Interval(s.file_loc, hole->left));
        memcpy(buffer, data_read.data(), data_read.size());
        return data_read.size();

    // If we run into a situation where we either do not have enough bytes to fill, or the file_loc is in an uncovered region.
    } else {
        return 0;
    }
}

int fs_seek_client(ClientData& s, int64_t pos) {
    if (pos >= s.cache.get_size()) {
        return -1;
    } else {
        return static_cast<int>(s.file_loc);
    }
}

int64_t fs_get_size_client(ClientData& s) {
    return s.cache.get_size();
}

void fs_close_client(ClientData& s) {
    s.cache.clear_cache();
}