#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>

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
    std::optional<uint64_t> find_unfilled_after(uint64_t start_point);
};
