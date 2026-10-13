#include "graphql.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace akashic {
namespace gql {

const Value* Value::field(const std::string& name) const {
  for (const auto& f : fields) {
    if (f.first == name) return &f.second;
  }
  return nullptr;
}

std::string quote(const std::string& text) {
  std::string out = "\"";
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out + "\"";
}

namespace {

/** A selected field: its alias (or name), name, and its own selections. */
struct Field {
  std::string key;
  std::string name;
  bool hasSelection = false;
  std::vector<Field> selection;
};

/** Reads a query document. */
class Parser {
 public:
  explicit Parser(const std::string& s) : s_(s) {}

  /** The operation's selection, or false with `error` set. */
  bool document(std::vector<Field>& out, std::string& error) {
    skip();
    if (peekName()) {
      const std::string kw = name();
      if (kw == "mutation" || kw == "subscription") return fail(error, "Only queries are supported");
      if (kw == "fragment") return fail(error, "Fragments are not supported");
      if (kw != "query") return fail(error, "Syntax Error: Unexpected Name \"" + kw + "\"");
      skip();
      if (peekName()) name();
      skip();
      // Variable definitions: none of our fields take arguments, so they're unused.
      if (peek('(') && !skipParens()) return fail(error, "Syntax Error: Unbalanced parentheses");
      skip();
      if (peek('@')) return fail(error, "Directives are not supported");
    }
    if (!selection(out, 1, error)) return false;
    skip();
    if (i_ < s_.size()) return fail(error, s_.compare(i_, 3, "...") == 0 || peekName() ? "Only one operation is supported" : "Syntax Error: Unexpected text after the query");
    return true;
  }

 private:
  const std::string& s_;
  size_t i_ = 0;

  static bool fail(std::string& error, const std::string& message) {
    error = message;
    return false;
  }
  void skip() {
    while (i_ < s_.size()) {
      const char c = s_[i_];
      if (c == '#') {
        while (i_ < s_.size() && s_[i_] != '\n') i_++;
      } else if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
        i_++;
      } else {
        break;
      }
    }
  }
  bool peek(char c) const { return i_ < s_.size() && s_[i_] == c; }
  bool peekName() const { return i_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_'); }
  std::string name() {
    const size_t start = i_;
    while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) i_++;
    return s_.substr(start, i_ - start);
  }
  bool skipParens() {
    int depth = 0;
    bool inString = false;
    for (; i_ < s_.size(); i_++) {
      const char c = s_[i_];
      if (inString) {
        if (c == '\\') i_++;
        else if (c == '"') inString = false;
      } else if (c == '"') {
        inString = true;
      } else if (c == '(') {
        depth++;
      } else if (c == ')' && --depth == 0) {
        i_++;
        return true;
      }
    }
    return false;
  }
  bool selection(std::vector<Field>& out, int depth, std::string& error) {
    if (depth > kMaxDepth) return fail(error, "The query is nested too deeply");
    skip();
    if (!peek('{')) return fail(error, "Syntax Error: Expected \"{\"");
    i_++;
    for (;;) {
      skip();
      if (peek('}')) {
        i_++;
        return out.empty() ? fail(error, "Syntax Error: Expected Name, found \"}\"") : true;
      }
      if (s_.compare(i_, 3, "...") == 0) return fail(error, "Fragments are not supported");
      if (!peekName()) return fail(error, i_ >= s_.size() ? "Syntax Error: Expected \"}\"" : "Syntax Error: Expected Name");
      Field f;
      f.key = f.name = name();
      skip();
      if (peek(':')) {
        i_++;
        skip();
        if (!peekName()) return fail(error, "Syntax Error: Expected Name after an alias");
        f.name = name();
        skip();
      }
      if (peek('(')) return fail(error, "Field \"" + f.name + "\" takes no arguments");
      if (peek('@')) return fail(error, "Directives are not supported");
      if (peek('{')) {
        f.hasSelection = true;
        if (!selection(f.selection, depth + 1, error)) return false;
      }
      out.push_back(f);
    }
  }
};

void number(std::string& out, double n) {
  if (!std::isfinite(n)) {
    out += "null";
    return;
  }
  char buf[32];
  if (n == std::floor(n) && std::fabs(n) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", n);
  else std::snprintf(buf, sizeof buf, "%.6g", n);
  out += buf;
}

/** Write a value through its selection; false (with `error`) on a field that doesn't fit. */
bool write(std::string& out, const Value& v, const Field* sel, std::string& error);

bool writeObject(std::string& out, const Value& v, const std::vector<Field>& selection, std::string& error) {
  out += '{';
  bool first = true;
  for (const Field& f : selection) {
    if (!first) out += ',';
    first = false;
    out += quote(f.key);
    out += ':';
    if (f.name == "__typename") {
      if (f.hasSelection) return (error = "Field \"__typename\" must not have a selection since type \"String\" has no subfields.", false);
      out += quote(v.type);
      continue;
    }
    const Value* child = v.field(f.name);
    if (!child) return (error = "Cannot query field \"" + f.name + "\" on type \"" + v.type + "\".", false);
    if (!write(out, *child, &f, error)) return false;
  }
  out += '}';
  return true;
}

bool write(std::string& out, const Value& v, const Field* sel, std::string& error) {
  switch (v.kind) {
    case Value::Kind::Object:
      if (!sel->hasSelection) return (error = "Field \"" + sel->name + "\" of type \"" + v.type + "\" must have a selection of subfields.", false);
      return writeObject(out, v, sel->selection, error);
    case Value::Kind::List:
      out += '[';
      for (size_t i = 0; i < v.items.size(); i++) {
        if (i) out += ',';
        if (!write(out, v.items[i], sel, error)) return false;
      }
      out += ']';
      return true;
    default:
      if (sel->hasSelection) return (error = "Field \"" + sel->name + "\" must not have a selection since it has no subfields.", false);
      if (v.kind == Value::Kind::Number) number(out, v.number);
      else if (v.kind == Value::Kind::Text) out += quote(v.text);
      else if (v.kind == Value::Kind::Bool) out += v.flag ? "true" : "false";
      else out += "null";
      return true;
  }
}

/** Reads one JSON value, keeping a string's text. */
class Json {
 public:
  explicit Json(const std::string& s) : s_(s) {}
  void ws() {
    while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) i_++;
  }
  bool string(std::string& out) {
    if (i_ >= s_.size() || s_[i_] != '"') return false;
    i_++;
    while (i_ < s_.size()) {
      const char c = s_[i_++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i_ >= s_.size()) return false;
      const char e = s_[i_++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          if (i_ + 4 > s_.size()) return false;
          unsigned code = 0;
          for (int k = 0; k < 4; k++) {
            const char h = s_[i_++];
            const int d = std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
            if (d < 0) return false;
            code = code * 16 + static_cast<unsigned>(d);
          }
          // Queries are ASCII in practice; other characters are written as UTF-8.
          if (code < 0x80) {
            out += static_cast<char>(code);
          } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
          } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
          }
          break;
        }
        default: out += e;
      }
    }
    return false;
  }
  /** Read any value into `out` (objects get no type). */
  bool value(Value& out, int depth = 0) {
    if (depth > 32) return false;
    ws();
    if (i_ >= s_.size()) return false;
    const char c = s_[i_];
    if (c == '"') {
      out = Value::str("");
      return string(out.text);
    }
    if (c == '{' || c == '[') {
      const bool obj = c == '{';
      out = obj ? Value::object("") : Value::list();
      const char close = obj ? '}' : ']';
      i_++;
      ws();
      if (i_ < s_.size() && s_[i_] == close) return ++i_, true;
      for (;;) {
        std::string key;
        if (obj) {
          ws();
          if (!string(key)) return false;
          ws();
          if (i_ >= s_.size() || s_[i_++] != ':') return false;
        }
        Value item;
        if (!value(item, depth + 1)) return false;
        if (obj) out.set(key, item);
        else out.add(item);
        ws();
        if (i_ >= s_.size()) return false;
        if (s_[i_] == ',') {
          i_++;
          continue;
        }
        return s_[i_++] == close;
      }
    }
    const size_t start = i_;
    while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.')) i_++;
    const std::string word = s_.substr(start, i_ - start);
    if (word == "true" || word == "false") {
      out = Value::boolean(word == "true");
    } else if (word == "null") {
      out = Value::null();
    } else {
      char* end = nullptr;
      const double n = std::strtod(word.c_str(), &end);
      if (word.empty() || *end) return false;
      out = Value::num(n);
    }
    return true;
  }
  bool done() {
    ws();
    return i_ == s_.size();
  }
  /** Skip any value. */
  bool skipValue(int depth = 0) {
    if (depth > 32) return false;
    ws();
    if (i_ >= s_.size()) return false;
    const char c = s_[i_];
    if (c == '"') {
      std::string ignored;
      return string(ignored);
    }
    if (c == '{' || c == '[') {
      const char close = c == '{' ? '}' : ']';
      i_++;
      ws();
      if (i_ < s_.size() && s_[i_] == close) return ++i_, true;
      for (;;) {
        if (c == '{') {
          ws();
          std::string key;
          if (!string(key)) return false;
          ws();
          if (i_ >= s_.size() || s_[i_++] != ':') return false;
        }
        if (!skipValue(depth + 1)) return false;
        ws();
        if (i_ >= s_.size()) return false;
        if (s_[i_] == ',') {
          i_++;
          continue;
        }
        return s_[i_++] == close;
      }
    }
    // A number or a literal.
    const size_t start = i_;
    while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.')) i_++;
    return i_ > start;
  }
  /** The top-level object's string member `name`. */
  bool member(const std::string& name, std::string& out) {
    ws();
    if (i_ >= s_.size() || s_[i_++] != '{') return false;
    bool found = false;
    ws();
    if (i_ < s_.size() && s_[i_] == '}') return false;
    for (;;) {
      ws();
      std::string key;
      if (!string(key)) return false;
      ws();
      if (i_ >= s_.size() || s_[i_++] != ':') return false;
      ws();
      if (key == name && i_ < s_.size() && s_[i_] == '"') {
        out.clear();
        if (!string(out)) return false;
        found = true;
      } else if (!skipValue()) {
        return false;
      }
      ws();
      if (i_ >= s_.size()) return false;
      if (s_[i_] == ',') {
        i_++;
        continue;
      }
      return s_[i_] == '}' && found;
    }
  }

 private:
  const std::string& s_;
  size_t i_ = 0;
};

std::string errors(const std::string& message) { return "{\"errors\":[{\"message\":" + quote(message) + "}]}"; }

}  // namespace

std::string execute(const std::string& query, const Value& root) {
  if (query.size() > kMaxQuery) return errors("The query is too long");
  std::vector<Field> selection;
  std::string error;
  Parser p(query);
  if (!p.document(selection, error)) return errors(error);
  std::string data;
  if (!writeObject(data, root, selection, error)) return errors(error);
  return "{\"data\":" + data + "}";
}

bool parseJson(const std::string& text, Value& out) {
  Json j(text);
  return j.value(out) && j.done();
}

bool queryFromBody(const std::string& body, std::string& query) {
  if (body.size() > kMaxQuery * 2) return false;
  Json j(body);
  return j.member("query", query);
}

}  // namespace gql
}  // namespace akashic
