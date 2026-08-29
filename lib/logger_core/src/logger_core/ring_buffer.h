#pragma once

#include <stddef.h>

namespace logger_core {

template <typename T, size_t Capacity>
class RingBuffer {
  static_assert(Capacity > 0, "RingBuffer capacity must be greater than zero");

 public:
  bool push(const T& value) {
    if (count_ == Capacity) return false;
    values_[(head_ + count_) % Capacity] = value;
    ++count_;
    return true;
  }

  bool front(T& value) const {
    return get(0, value);
  }

  bool back(T& value) const {
    return count_ > 0 && get(count_ - 1, value);
  }

  bool pop(T& value) {
    if (!front(value)) return false;
    head_ = (head_ + 1) % Capacity;
    --count_;
    return true;
  }

  bool get(size_t index, T& value) const {
    if (index >= count_) return false;
    value = values_[(head_ + index) % Capacity];
    return true;
  }

  size_t size() const {
    return count_;
  }

  constexpr size_t capacity() const {
    return Capacity;
  }

  bool empty() const {
    return count_ == 0;
  }

  void clear() {
    head_ = 0;
    count_ = 0;
  }

 private:
  T values_[Capacity]{};
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace logger_core
