#include "logger_core/http_parser.h"

#include <string.h>

namespace logger_core {
namespace {

bool isMethodChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '!' || c == '#' || c == '$' ||
         c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
         c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
         c == '|' || c == '~';
}

bool equals(const char* value, size_t length, const char* expected) {
  const size_t expectedLength = strlen(expected);
  return length == expectedLength &&
         memcmp(value, expected, expectedLength) == 0;
}

HttpRoute routeTarget(const char* target, size_t targetLength) {
  if (equals(target, targetLength, "/")) return HttpRoute::INDEX;
  if (equals(target, targetLength, "/status")) return HttpRoute::STATUS;
  if (equals(target, targetLength, "/probe")) return HttpRoute::PROBE;
  if (equals(target, targetLength, "/reset")) return HttpRoute::RESET;
  return HttpRoute::UNKNOWN;
}

}  // namespace

RequestLineReadResult consumeRequestLineByte(RequestLineReader& reader,
                                             char byte) {
  if (reader.complete) return RequestLineReadResult::COMPLETE;
  if (reader.awaitingLf) {
    if (byte != '\n') return RequestLineReadResult::BAD_REQUEST;
    if (reader.length == 0) return RequestLineReadResult::BAD_REQUEST;
    reader.line[reader.length] = '\0';
    reader.complete = true;
    reader.awaitingLf = false;
    return RequestLineReadResult::COMPLETE;
  }
  if (byte == '\r') {
    reader.awaitingLf = true;
    return RequestLineReadResult::IN_PROGRESS;
  }
  if (byte == '\n') return RequestLineReadResult::BAD_REQUEST;
  if (reader.length == HTTP_MAX_REQUEST_LINE_BYTES) {
    return RequestLineReadResult::TOO_LONG;
  }
  reader.line[reader.length++] = byte;
  return RequestLineReadResult::IN_PROGRESS;
}

RequestLineReadResult finishRequestLine(const RequestLineReader& reader) {
  return reader.complete ? RequestLineReadResult::COMPLETE
                         : RequestLineReadResult::BAD_REQUEST;
}

HttpRequestParseResult parseHttpRequestLine(const char* line, size_t length,
                                            HttpRequest& request) {
  request = HttpRequest{};
  if (line == nullptr || length == 0) return HttpRequestParseResult::BAD_REQUEST;
  if (length > HTTP_MAX_REQUEST_LINE_BYTES) {
    return HttpRequestParseResult::URI_TOO_LONG;
  }

  size_t methodEnd = 0;
  while (methodEnd < length && line[methodEnd] != ' ') {
    if (!isMethodChar(line[methodEnd])) {
      return HttpRequestParseResult::BAD_REQUEST;
    }
    ++methodEnd;
  }
  if (methodEnd == 0 || methodEnd == length) {
    return HttpRequestParseResult::BAD_REQUEST;
  }

  const size_t targetStart = methodEnd + 1;
  if (targetStart >= length || line[targetStart] == ' ') {
    return HttpRequestParseResult::BAD_REQUEST;
  }

  size_t targetEnd = targetStart;
  while (targetEnd < length && line[targetEnd] != ' ') {
    const unsigned char c = static_cast<unsigned char>(line[targetEnd]);
    if (c <= 0x20U || c == 0x7FU || c == '#') {
      return HttpRequestParseResult::BAD_REQUEST;
    }
    ++targetEnd;
  }
  if (targetEnd == length || targetEnd + 1 >= length ||
      line[targetEnd + 1] == ' ') {
    return HttpRequestParseResult::BAD_REQUEST;
  }

  const size_t targetLength = targetEnd - targetStart;
  if (targetLength > HTTP_MAX_REQUEST_TARGET_BYTES) {
    return HttpRequestParseResult::URI_TOO_LONG;
  }
  if (targetLength == 0 || line[targetStart] != '/') {
    return HttpRequestParseResult::BAD_REQUEST;
  }

  const char* version = line + targetEnd + 1;
  const size_t versionLength = length - targetEnd - 1;
  if (!equals(version, versionLength, "HTTP/1.0") &&
      !equals(version, versionLength, "HTTP/1.1")) {
    return HttpRequestParseResult::BAD_REQUEST;
  }

  size_t pathLength = targetLength;
  for (size_t i = 0; i < targetLength; ++i) {
    if (line[targetStart + i] == '?') {
      pathLength = i;
      break;
    }
  }
  if (pathLength == 0) return HttpRequestParseResult::BAD_REQUEST;

  request.method =
      equals(line, methodEnd, "GET") ? HttpMethod::GET : HttpMethod::OTHER;
  request.targetOffset = targetStart;
  request.targetLength = targetLength;
  request.pathLength = pathLength;
  request.hasQuery = pathLength != targetLength;
  // Current endpoints intentionally do not accept queries. Route the complete
  // target so "/status?x" remains unknown rather than matching "/status".
  request.route = routeTarget(line + targetStart, targetLength);
  return HttpRequestParseResult::OK;
}

HttpRouteDecision routeHttpRequest(const HttpRequest& request) {
  if (request.route == HttpRoute::UNKNOWN) {
    return HttpRouteDecision::NOT_FOUND;
  }
  if (request.method != HttpMethod::GET) {
    return HttpRouteDecision::METHOD_NOT_ALLOWED;
  }
  switch (request.route) {
    case HttpRoute::INDEX: return HttpRouteDecision::INDEX;
    case HttpRoute::STATUS: return HttpRouteDecision::STATUS;
    case HttpRoute::PROBE: return HttpRouteDecision::PROBE;
    case HttpRoute::RESET: return HttpRouteDecision::RESET;
    case HttpRoute::UNKNOWN: return HttpRouteDecision::NOT_FOUND;
  }
  return HttpRouteDecision::NOT_FOUND;
}

HeaderParseResult consumeHeaderByte(HeaderParser& parser, char byte) {
  if (parser.complete) return HeaderParseResult::COMPLETE;
  if (parser.totalBytes == HTTP_MAX_HEADER_BYTES) {
    return HeaderParseResult::TOO_LARGE;
  }
  ++parser.totalBytes;

  if (parser.awaitingLf) {
    if (byte != '\n') return HeaderParseResult::BAD_REQUEST;
    parser.awaitingLf = false;
    if (parser.lineBytes == 0) {
      parser.complete = true;
      return HeaderParseResult::COMPLETE;
    }
    ++parser.headerCount;
    if (parser.headerCount > HTTP_MAX_HEADER_COUNT) {
      return HeaderParseResult::TOO_LARGE;
    }
    parser.lineBytes = 0;
    return HeaderParseResult::IN_PROGRESS;
  }

  if (byte == '\r') {
    parser.awaitingLf = true;
    return HeaderParseResult::IN_PROGRESS;
  }
  if (byte == '\n') return HeaderParseResult::BAD_REQUEST;
  const unsigned char value = static_cast<unsigned char>(byte);
  if ((value < 0x20U && byte != '\t') || value == 0x7FU) {
    return HeaderParseResult::BAD_REQUEST;
  }
  if (parser.lineBytes == HTTP_MAX_HEADER_LINE_BYTES) {
    return HeaderParseResult::TOO_LARGE;
  }
  ++parser.lineBytes;
  return HeaderParseResult::IN_PROGRESS;
}

HeaderParseResult finishHeaders(const HeaderParser& parser) {
  if (parser.complete) return HeaderParseResult::COMPLETE;
  return HeaderParseResult::BAD_REQUEST;
}

}  // namespace logger_core
