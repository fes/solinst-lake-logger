#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logger_core {

constexpr size_t HTTP_MAX_REQUEST_LINE_BYTES = 256;
constexpr size_t HTTP_MAX_REQUEST_TARGET_BYTES = 192;
constexpr size_t HTTP_MAX_HEADER_LINE_BYTES = 256;
constexpr size_t HTTP_MAX_HEADER_BYTES = 2048;
constexpr size_t HTTP_MAX_HEADER_COUNT = 32;

enum class HttpMethod : uint8_t {
  GET,
  OTHER
};

enum class HttpRoute : uint8_t {
  INDEX,
  STATUS,
  PROBE,
  RESET,
  UNKNOWN
};

enum class HttpRequestParseResult : uint8_t {
  OK,
  BAD_REQUEST,
  URI_TOO_LONG
};

struct HttpRequest {
  HttpMethod method = HttpMethod::OTHER;
  HttpRoute route = HttpRoute::UNKNOWN;
  size_t targetOffset = 0;
  size_t targetLength = 0;
  size_t pathLength = 0;
  bool hasQuery = false;
};

enum class HttpRouteDecision : uint8_t {
  INDEX,
  STATUS,
  PROBE,
  RESET,
  NOT_FOUND,
  METHOD_NOT_ALLOWED
};

enum class RequestLineReadResult : uint8_t {
  IN_PROGRESS,
  COMPLETE,
  BAD_REQUEST,
  TOO_LONG
};

struct RequestLineReader {
  char line[HTTP_MAX_REQUEST_LINE_BYTES + 1] = {};
  size_t length = 0;
  bool awaitingLf = false;
  bool complete = false;
};

RequestLineReadResult consumeRequestLineByte(RequestLineReader& reader,
                                             char byte);
RequestLineReadResult finishRequestLine(const RequestLineReader& reader);
HttpRequestParseResult parseHttpRequestLine(const char* line, size_t length,
                                            HttpRequest& request);
HttpRouteDecision routeHttpRequest(const HttpRequest& request);

enum class HeaderParseResult : uint8_t {
  IN_PROGRESS,
  COMPLETE,
  BAD_REQUEST,
  TOO_LARGE
};

struct HeaderParser {
  size_t lineBytes = 0;
  size_t totalBytes = 0;
  size_t headerCount = 0;
  bool awaitingLf = false;
  bool complete = false;
};

HeaderParseResult consumeHeaderByte(HeaderParser& parser, char byte);
HeaderParseResult finishHeaders(const HeaderParser& parser);

}  // namespace logger_core
