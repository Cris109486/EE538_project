#include "ee538/resources.hpp"
#include <stdexcept>
namespace ee538 {
KVResourceTracker::KVResourceTracker(Tokens capacity) : capacity_(capacity) {
    if (capacity <= 0) throw std::invalid_argument("KV capacity must be positive");
}
bool KVResourceTracker::reserve(RequestId id, Tokens tokens) {
    if (tokens <= 0 || owns(id)) throw std::invalid_argument("invalid or duplicate reservation");
    if (tokens > available()) return false;
    reservations_.emplace(id, tokens);
    used_ += tokens;
    if (used_ > peak_) peak_ = used_;
    return true;
}
Tokens KVResourceTracker::release(RequestId id) {
    const auto it = reservations_.find(id);
    if (it == reservations_.end()) throw std::logic_error("release without reservation");
    const auto tokens = it->second;
    used_ -= tokens;
    reservations_.erase(it);
    return tokens;
}
bool KVResourceTracker::invariant() const {
    Tokens sum = 0;
    for (const auto& item : reservations_) {
        if (item.second <= 0 || item.second > capacity_ - sum) return false;
        sum += item.second;
    }
    return sum == used_ && used_ >= 0 && used_ <= capacity_ && peak_ >= used_ && peak_ <= capacity_;
}
} // namespace ee538
