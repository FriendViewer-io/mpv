#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <mutex>
#include <fstream>
#include <vector>
#include <string>


struct Interval {
    uint64_t left;
    uint64_t right;

    Interval(uint64_t l, uint64_t r) {
        left = l;
        right = r;
    }

    bool overlaps (Interval iv) {
        if ((iv.left <= left && iv.right >= left) || 
            (left <= iv.left && right >= iv.left)) {
                return true;
        }
        return false;
    }

    void accrue (Interval iv) {
        left = std::min(left, iv.left);
        right = std::max(right, iv.right);
    } 
};

class FileCacheTracker {
private:
    size_t size;
    std::list<Interval> coverage;

public:
    FileCacheTracker(size_t file_size);
    void add_interval(Interval iv);
    bool query_interval(Interval iv);
    std::optional<Interval> find_unfilled_after(uint64_t start_point);
    uint64_t find_filled_after (uint64_t start_point);
};

class FileCache {
    private:
    FileCacheTracker* tracker;
    std::fstream file_io;
    std::mutex file_lock;

    public:
    void create_cache (std::string name, size_t file_size);
    void write_data (void const* data, size_t len, size_t offset);
    bool has_data_at (Interval file_interval);
    std::optional<Interval> FileCache::get_first_missing_interval (Interval iv);
    std::vector<uint8_t> read_data (Interval iv);
};
