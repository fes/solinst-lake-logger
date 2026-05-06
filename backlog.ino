bool enqueueReading(const ProbeReading& r) {
  if (backlogCount >= BACKLOG_CAPACITY) {
    return false;
  }
  backlog[backlogCount++] = r;
  return true;
}

bool dequeueReading(ProbeReading& out) {
  if (backlogCount == 0) {
    return false;
  }
  out = backlog[0];
  for (size_t i = 1; i < backlogCount; i++) {
    backlog[i - 1] = backlog[i];
  }
  backlogCount--;
  return true;
}

bool peekBacklog(ProbeReading& out) {
  if (backlogCount == 0) {
    return false;
  }
  out = backlog[0];
  return true;
}