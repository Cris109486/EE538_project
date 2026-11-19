#pragma once
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ee538 {
// Compare(a,b)==true means a must appear before b (std::less -> minimum first).
template<class T, class Compare = std::less<T>> class BinaryHeap {
    std::vector<T> data_;
    Compare before_;
public:
    explicit BinaryHeap(Compare before = Compare{}) : before_(std::move(before)) {}
    bool empty() const noexcept { return data_.empty(); }
    std::size_t size() const noexcept { return data_.size(); }
    const T& top() const {
        if (empty()) throw std::out_of_range("heap is empty");
        return data_.front();
    }
    void push(T value) {
        data_.push_back(std::move(value));
        auto child = data_.size() - 1;
        while (child > 0) {
            auto parent = (child - 1) / 2;
            if (!before_(data_[child], data_[parent])) break;
            std::swap(data_[child], data_[parent]);
            child = parent;
        }
    }
    T pop() {
        if (empty()) throw std::out_of_range("heap is empty");
        T result = std::move(data_.front());
        if (data_.size() == 1) { data_.pop_back(); return result; }
        data_.front() = std::move(data_.back());
        data_.pop_back();
        std::size_t parent = 0;
        while (parent < data_.size() / 2) {
            auto child = 2 * parent + 1;
            if (child + 1 < data_.size() && before_(data_[child + 1], data_[child])) ++child;
            if (!before_(data_[child], data_[parent])) break;
            std::swap(data_[child], data_[parent]);
            parent = child;
        }
        return result;
    }
    bool invariant() const {
        for (std::size_t i = 1; i < data_.size(); ++i)
            if (before_(data_[i], data_[(i - 1) / 2])) return false;
        return true;
    }
};

// Owned bounded history. Only initialized elements are stored; T need not be default constructible.
template<class T> class RingBuffer {
    std::vector<T> data_;
    std::size_t capacity_;
    std::size_t next_{};
public:
    explicit RingBuffer(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0) throw std::invalid_argument("ring capacity must be positive");
        data_.reserve(capacity);
    }
    void push(T value) {
        if (data_.size() < capacity_) data_.push_back(std::move(value));
        else data_[next_] = std::move(value);
        next_ = (next_ + 1) % capacity_;
    }
    std::size_t size() const noexcept { return data_.size(); }
    bool invariant() const noexcept { return data_.size() <= capacity_ && next_ < capacity_; }
    std::vector<T> values() const {
        std::vector<T> result;
        result.reserve(size());
        const auto first = size() == capacity_ ? next_ : 0;
        for (std::size_t i = 0; i < size(); ++i) result.push_back(data_[(first + i) % size()]);
        return result;
    }
};
} // namespace ee538
