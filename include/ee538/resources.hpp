#pragma once
#include "ee538/types.hpp"
#include <map>
namespace ee538 {
class KVResourceTracker {
    Tokens capacity_;
    Tokens used_{};
    Tokens peak_{};
    std::map<RequestId, Tokens> reservations_;
public:
    explicit KVResourceTracker(Tokens capacity);
    bool reserve(RequestId request, Tokens tokens);
    Tokens release(RequestId request); // exactly once; invalid release throws
    bool owns(RequestId request) const { return reservations_.count(request) != 0; }
    Tokens used() const noexcept { return used_; }
    Tokens available() const noexcept { return capacity_ - used_; }
    Tokens peak() const noexcept { return peak_; }
    bool invariant() const;
};
} // namespace ee538
