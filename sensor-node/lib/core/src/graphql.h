// A small GraphQL query executor for the node's own endpoint: queries (no mutations), field
// selections and aliases, `__typename`, comments, and `query Name` with variable definitions
// it doesn't need. No fragments, arguments or directives: the node's fields take none.
//
// The answer is computed from a snapshot (a tree of Values) and only the selected fields
// are written, as `{"data": …}` or `{"errors": [{"message": …}]}`. Plain C++, tested on a
// computer.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace akashic {
namespace gql {

/** A value in the snapshot: null, a number, text, true/false, a list or a typed object. */
struct Value {
  enum class Kind { Null, Number, Text, Bool, List, Object };
  Kind kind = Kind::Null;
  double number = 0;
  std::string text;
  bool flag = false;
  /** An object's GraphQL type name. */
  std::string type;
  std::vector<std::pair<std::string, Value>> fields;
  std::vector<Value> items;

  static Value null() { return Value(); }
  static Value num(double n) {
    Value v;
    v.kind = Kind::Number;
    v.number = n;
    return v;
  }
  static Value str(const std::string& s) {
    Value v;
    v.kind = Kind::Text;
    v.text = s;
    return v;
  }
  static Value boolean(bool b) {
    Value v;
    v.kind = Kind::Bool;
    v.flag = b;
    return v;
  }
  static Value object(const std::string& typeName) {
    Value v;
    v.kind = Kind::Object;
    v.type = typeName;
    return v;
  }
  static Value list() {
    Value v;
    v.kind = Kind::List;
    return v;
  }
  /** Add an object's field (returns this object, to chain). */
  Value& set(const std::string& name, const Value& value) {
    fields.emplace_back(name, value);
    return *this;
  }
  /** Add a list item. */
  Value& add(const Value& value) {
    items.push_back(value);
    return *this;
  }
  /** A field (null when there is none). */
  const Value* field(const std::string& name) const;
};

/** The longest query accepted, in bytes, and the deepest selection. */
constexpr size_t kMaxQuery = 4096;
constexpr int kMaxDepth = 8;

/** Run a query on a snapshot whose root is the `Query` object; the JSON answer. */
std::string execute(const std::string& query, const Value& root);

/**
 * Read `query` from a GraphQL-over-HTTP JSON body (`{"query": "…", "variables": …}`).
 * @return False when the body isn't a JSON object with a string `query`.
 */
bool queryFromBody(const std::string& body, std::string& query);

/** Parse JSON into a Value (objects get no type); false when it isn't JSON. */
bool parseJson(const std::string& text, Value& out);

/** A JSON string literal, quoted and escaped. */
std::string quote(const std::string& text);

}  // namespace gql
}  // namespace akashic
