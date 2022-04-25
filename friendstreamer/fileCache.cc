#include <iostream>
#include <list>
#include <optional>
#include <algorithm>
using namespace std;

struct Interval {
    uint64_t left;
    uint64_t right;

    Interval(uint64_t l, uint64_t r) {
        left = l;
        right = r;
    }

    bool overlaps (Interval iv) {
        if ((iv.left <= left && iv.right >= left) || 
            (left <= iv.left && right >= iv.left) ||
            (iv.left <= right && iv.right >= right) ||
            (iv.left >= left && iv.right <= right)) {
                return true;
        }
        return false;
    }

    void accrue (Interval iv) {
        left = min(left, iv.left);
        right = max(right, iv.right);
    } 
};


class FileCacheTracker {
    public:
    size_t size;
    std::list<Interval> coverage;

    FileCacheTracker(size_t file_size) {
        size = file_size;
    }

    void add_interval (Interval iv) {    
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

    bool query_interval (Interval iv) {
        for (auto it = coverage.begin(); it != coverage.end(); it++) {
            if (it->overlaps(iv)) {
                return true;
            }
        }
        return false;
    }

    std::optional<uint64_t> find_unfilled_after(uint64_t start_point) {
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
                    return nullopt;
                } else {
                    return it->right;
                }
            } 
        }
        return start_point;
    }
};