#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace logger_core {

template <size_t BucketCount, uint32_t BucketSeconds>
class RollingExtrema {
  static_assert(BucketCount > 0, "RollingExtrema needs at least one bucket");
  static_assert(BucketSeconds > 0, "RollingExtrema bucket duration must be positive");

 public:
  bool add(int64_t epochSeconds, float value) {
    if (epochSeconds < 0 || !isfinite(value)) return false;
    const int64_t key = epochSeconds / BucketSeconds;
    Bucket& bucket = buckets_[static_cast<size_t>(key % BucketCount)];
    if (bucket.key != key) {
      bucket.key = key;
      bucket.minimum = value;
      bucket.maximum = value;
      return true;
    }
    if (value < bucket.minimum) bucket.minimum = value;
    if (value > bucket.maximum) bucket.maximum = value;
    return true;
  }

  bool extrema(int64_t nowEpochSeconds, float& minimum, float& maximum) const {
    if (nowEpochSeconds < 0) return false;
    const int64_t newestKey = nowEpochSeconds / BucketSeconds;
    const int64_t oldestKey =
        newestKey - static_cast<int64_t>(BucketCount) + 1;
    bool found = false;
    for (const Bucket& bucket : buckets_) {
      if (bucket.key < oldestKey || bucket.key > newestKey) continue;
      if (!found) {
        minimum = bucket.minimum;
        maximum = bucket.maximum;
        found = true;
      } else {
        if (bucket.minimum < minimum) minimum = bucket.minimum;
        if (bucket.maximum > maximum) maximum = bucket.maximum;
      }
    }
    return found;
  }

  void clear() {
    for (Bucket& bucket : buckets_) bucket = Bucket{};
  }

 private:
  struct Bucket {
    int64_t key = INT64_MIN;
    float minimum = NAN;
    float maximum = NAN;
  };

  Bucket buckets_[BucketCount];
};

}  // namespace logger_core
