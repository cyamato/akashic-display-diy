// Reads a Server-Sent Events stream (the server's tunnel): `event:`, `id:` and `data:` lines,
// an event ending at a blank line, comments (`:`) ignored.
#pragma once

#include <cstdint>
#include <string>

namespace akashic {

/** An https URL's parts. */
struct UrlParts {
  std::string host;
  uint16_t port = 443;
  /** The path and query, at least "/". */
  std::string path = "/";
};

/** Split an https URL (no user info); false for anything else. */
bool splitUrl(const std::string& url, UrlParts& out);

/** Undoes HTTP/1.1 chunked transfer encoding, one character at a time. */
class ChunkedReader {
 public:
  /** Feed one received character; true when it is body data (then `out` holds it). */
  bool feed(char c, char& out);
  /** Whether the last (zero-size) chunk arrived, or the encoding was broken. */
  bool finished() const { return state_ == State::Done; }
  bool broken() const { return broken_; }

 private:
  enum class State { Size, SizeExt, SizeLf, Data, DataCr, DataLf, Done };
  State state_ = State::Size;
  uint32_t left_ = 0;
  int digits_ = 0;
  bool broken_ = false;
};

struct SseEvent {
  std::string event;
  std::string id;
  std::string data;
};

class SseReader {
 public:
  /** Feed one character; true when it completes an event (then `event()` holds it). */
  bool feed(char c);
  const SseEvent& event() const { return done_; }

  /** The longest line kept; longer ones are dropped (the server sends small questions). */
  static constexpr size_t kMaxLine = 8192;

 private:
  void line();
  std::string line_;
  bool tooLong_ = false;
  SseEvent building_;
  SseEvent done_;
  bool ready_ = false;
  bool hasData_ = false;
};

}  // namespace akashic
