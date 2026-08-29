#include "config.h"

bool enqueueReading(const ProbeReading& r) {
  return backlogStorage.enqueue(r);
}

bool dequeueReading(ProbeReading& out) {
  return backlogStorage.dequeue(out);
}

bool peekBacklog(ProbeReading& out) {
  return backlogStorage.peek(out);
}