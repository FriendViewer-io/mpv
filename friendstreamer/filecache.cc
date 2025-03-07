#include "filecache.hh"

#include <algorithm>
#include <iostream>
#include <list>
#include <optional>
#include <string>

FileCacheTracker::FileCacheTracker(size_t file_size) {
    size = file_size;
}

void FileCacheTracker::add_interval(Interval iv) {    
    iv.right = std::min(size, iv.right);
    for (auto it = coverage.begin(); it != coverage.end(); it++) {
        if (it->overlaps(iv)) {
            it->accrue(iv);
            
            while (std::next(it, 1) != coverage.end()) {
                auto next = std::next(it, 1);
                if (it->overlaps(Interval(next->left, next->right))) {
                    it->accrue(Interval(next->left, next->right));
                    coverage.erase(next);
                } else {
                    break;
                }
            }
            return;
        // If we're between the current and next node.    
        } else if (it->left > iv.right) {
            coverage.insert(it, iv);
            return;
        } 
    }  
    coverage.push_back(iv);
}

bool FileCacheTracker::query_interval(Interval iv) {
    for (auto it = coverage.begin(); it != coverage.end(); it++) {
        if (it->overlaps(iv)) {
            return true;
        }
    }
    return false;
}

uint64_t FileCacheTracker::find_filled_after(uint64_t start_point) {
    for (auto it = coverage.begin(); it != coverage.end(); it++) {
        if (it->left > start_point) {
            return it->left;
        }
    }
    return size;
}

size_t FileCacheTracker::get_size() {
    return size;
}

void FileCacheTracker::clear_list() {
    coverage.clear();
}

std::optional<Interval> FileCacheTracker::find_unfilled_after(uint64_t start_point) {
    // If the given start_point is out of bounds.
    if (start_point >= size) {
        return std::nullopt;
    }

    // Fake interval that will be used to find an overlap within our list.
    Interval sp = Interval(start_point, start_point);
    for (auto it = coverage.begin(); it != coverage.end(); it++) {
        if (it->overlaps(sp)) {
            // If the entire list is covered and there is no open space left.
            if (it->right == size) {
                return std::nullopt;
            } else {
                return Interval(it->right, find_filled_after(it->right));
            }
        } 
    }
    return Interval(start_point, find_filled_after(start_point));
}

FileCache::FileCache() : tracker(nullptr) {}

bool FileCache::create_cache(std::string filename, size_t file_size) {
    file_io.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    if (file_io.is_open()) {
        tracker = std::make_unique<FileCacheTracker>(file_size);
        return true;
    }
    return false;
}

void FileCache::write_data(void const* data, size_t len, size_t offset) {
    file_io.seekp(offset);
    file_io.write(reinterpret_cast<char const*>(data), len);
    file_io.flush();
    tracker->add_interval(Interval(offset, offset + len));
}

bool FileCache::has_data_at(Interval file_interval) {
    return tracker->query_interval(file_interval);
}

std::optional<Interval> FileCache::get_first_missing_interval(Interval iv) {
   std::optional<Interval> first_missing_interval = tracker->find_unfilled_after(iv.left);
   if (first_missing_interval == std::nullopt) {
       return std::nullopt;
   } else if (first_missing_interval->left >= iv.right) {
       return std::nullopt;
   } else if (first_missing_interval->right > iv.right) {
       return Interval(first_missing_interval->left, iv.right);
   } else {
       return first_missing_interval;
   }
}

std::vector<Interval> FileCache::get_all_missing_intervals(Interval iv) {
    // Clamp interval to filesize
    iv.right = std::min(iv.right, get_size());
    uint64_t search_from = iv.left;
    std::vector<Interval> missing_intervals;
    for (;;) {
        std::optional<Interval> missing_interval = tracker->find_unfilled_after(search_from);
        if (missing_interval == std::nullopt) {
            break;
        } else if (missing_interval->left >= iv.right) {
            break;
        } else if (missing_interval->right > iv.right) {
            missing_intervals.emplace_back(missing_interval->left, iv.right);
            break;
        } else {
            missing_intervals.emplace_back(*missing_interval);
            search_from = missing_interval->right;
        }
    }
    return missing_intervals;
}

std::vector<uint8_t> FileCache::read_data(Interval iv) {
    std::vector<uint8_t> buffer;
    const size_t buf_size = iv.right - iv.left;
    buffer.resize(buf_size);

    file_io.seekg(iv.left);
    file_io.read(reinterpret_cast<char*>(buffer.data()), buf_size);
    return buffer;

}

size_t FileCache::get_size() {
    return tracker->get_size();
}

void FileCache::clear_cache() {
    tracker->clear_list();
    std::remove(kStreambufName);
}
