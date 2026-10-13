#include "sse.h"

#include <cctype>

namespace akashic {

bool splitUrl(const std::string& url, UrlParts& out) {
  const std::string scheme = "https://";
  if (url.compare(0, scheme.size(), scheme) != 0) return false;
  const size_t start = scheme.size();
  size_t end = url.find_first_of("/?#", start);
  if (end == std::string::npos) end = url.size();
  const std::string authority = url.substr(start, end - start);
  if (authority.empty() || authority.find('@') != std::string::npos) return false;
  UrlParts p;
  const size_t colon = authority.rfind(':');
  if (colon != std::string::npos && authority.find(']') == std::string::npos) {
    const std::string port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    uint32_t n = 0;
    for (char c : port) {
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
      n = n * 10 + static_cast<uint32_t>(c - '0');
    }
    if (n == 0 || n > 65535) return false;
    p.port = static_cast<uint16_t>(n);
    p.host = authority.substr(0, colon);
  } else {
    p.host = authority;
  }
  if (p.host.empty()) return false;
  const size_t hash = url.find('#', end);
  p.path = url.substr(end, hash == std::string::npos ? std::string::npos : hash - end);
  if (p.path.empty() || p.path[0] != '/') p.path = "/" + p.path;
  out = p;
  return true;
}

bool ChunkedReader::feed(char c, char& out) {
  switch (state_) {
    case State::Size: {
      const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
      if (d >= 0 && digits_ < 7) {
        left_ = left_ * 16 + static_cast<uint32_t>(d);
        digits_++;
        return false;
      }
      if (digits_ == 0) break;
      if (c == ';' || c == ' ') {
        state_ = State::SizeExt;
        return false;
      }
      if (c == '\r') {
        state_ = State::SizeLf;
        return false;
      }
      break;
    }
    case State::SizeExt:
      if (c == '\r') state_ = State::SizeLf;
      return false;
    case State::SizeLf:
      if (c != '\n') break;
      digits_ = 0;
      state_ = left_ == 0 ? State::Done : State::Data;
      return false;
    case State::Data:
      out = c;
      if (--left_ == 0) state_ = State::DataCr;
      return true;
    case State::DataCr:
      if (c != '\r') break;
      state_ = State::DataLf;
      return false;
    case State::DataLf:
      if (c != '\n') break;
      state_ = State::Size;
      return false;
    case State::Done:
      return false;
  }
  broken_ = true;
  state_ = State::Done;
  return false;
}

bool SseReader::feed(char c) {
  ready_ = false;
  if (c == '\r') return false;
  if (c != '\n') {
    if (line_.size() < kMaxLine) line_ += c;
    else tooLong_ = true;
    return false;
  }
  line();
  return ready_;
}

void SseReader::line() {
  if (tooLong_) {
    // A line we couldn't keep spoils its event.
    tooLong_ = false;
    line_.clear();
    building_ = SseEvent();
    hasData_ = false;
    return;
  }
  if (line_.empty()) {
    if (hasData_) {
      done_ = building_;
      ready_ = true;
    }
    building_ = SseEvent();
    hasData_ = false;
    return;
  }
  if (line_[0] != ':') {
    const size_t colon = line_.find(':');
    const std::string field = line_.substr(0, colon);
    std::string value = colon == std::string::npos ? "" : line_.substr(colon + 1);
    if (!value.empty() && value[0] == ' ') value.erase(0, 1);
    if (field == "event") {
      building_.event = value;
    } else if (field == "id") {
      building_.id = value;
    } else if (field == "data") {
      if (hasData_) building_.data += '\n';
      building_.data += value;
      hasData_ = true;
    }
  }
  line_.clear();
}

}  // namespace akashic
