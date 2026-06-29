#include "map/MapParser.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <string>

namespace lg {

const std::string* MapEntity::property(std::string_view key) const {
  for (const MapProperty& property : properties) {
    if (property.key == key) {
      return &property.value;
    }
  }
  return nullptr;
}

namespace {

class Parser {
public:
  explicit Parser(std::string_view text) : text_(text) {}

  [[nodiscard]] MapParseResult parse() {
    MapDocument document;
    skipSpaceAndComments();
    while (!atEnd()) {
      if (!consume('{')) {
        return fail("expected entity '{'");
      }
      MapEntity entity;
      entity.line = line_;
      while (true) {
        skipSpaceAndComments();
        if (atEnd()) {
          return fail("unterminated entity");
        }
        if (consume('}')) {
          break;
        }
        if (peek() == '"') {
          MapProperty property;
          property.line = line_;
          if (!parseQuoted(property.key)) {
            return fail("expected quoted property key");
          }
          skipHorizontalSpace();
          if (!parseQuoted(property.value)) {
            return fail("expected quoted property value");
          }
          entity.properties.push_back(std::move(property));
        } else if (consume('{')) {
          MapBrush brush;
          brush.line = line_;
          while (true) {
            skipSpaceAndComments();
            if (atEnd()) {
              return fail("unterminated brush");
            }
            if (consume('}')) {
              break;
            }
            MapFace face;
            face.line = line_;
            if (!parseFace(face)) {
              return fail(lastError_.empty() ? "expected brush face" : lastError_);
            }
            brush.faces.push_back(std::move(face));
          }
          entity.brushes.push_back(std::move(brush));
        } else {
          return fail("expected property, brush, or entity end");
        }
      }
      document.entities.push_back(std::move(entity));
      skipSpaceAndComments();
    }
    return {std::move(document), true, {}};
  }

private:
  [[nodiscard]] bool atEnd() const {
    return offset_ >= text_.size();
  }

  [[nodiscard]] char peek() const {
    return atEnd() ? '\0' : text_[offset_];
  }

  [[nodiscard]] bool consume(char expected) {
    if (peek() != expected) {
      return false;
    }
    advance();
    return true;
  }

  void advance() {
    if (!atEnd() && text_[offset_] == '\n') {
      ++line_;
    }
    ++offset_;
  }

  void skipHorizontalSpace() {
    while (!atEnd() && (text_[offset_] == ' ' || text_[offset_] == '\t' || text_[offset_] == '\r')) {
      advance();
    }
  }

  void skipSpaceAndComments() {
    while (!atEnd()) {
      if (std::isspace(static_cast<unsigned char>(peek())) != 0) {
        advance();
      } else if (peek() == '/' && offset_ + 1 < text_.size() && text_[offset_ + 1] == '/') {
        while (!atEnd() && peek() != '\n') {
          advance();
        }
      } else {
        break;
      }
    }
  }

  [[nodiscard]] bool parseQuoted(std::string& value) {
    if (!consume('"')) {
      return false;
    }
    value.clear();
    while (!atEnd() && peek() != '"') {
      if (peek() == '\n') {
        return false;
      }
      if (peek() == '\\') {
        advance();
        if (atEnd()) {
          return false;
        }
      }
      value.push_back(peek());
      advance();
    }
    return consume('"');
  }

  [[nodiscard]] bool parseNumber(float& value) {
    skipHorizontalSpace();
    const std::size_t begin = offset_;
    while (!atEnd()) {
      const char c = peek();
      if (
        std::isdigit(static_cast<unsigned char>(c)) != 0 ||
        c == '-' ||
        c == '+' ||
        c == '.' ||
        c == 'e' ||
        c == 'E'
      ) {
        advance();
      } else {
        break;
      }
    }
    if (begin == offset_) {
      return false;
    }
    const char* first = text_.data() + begin;
    const char* last = text_.data() + offset_;
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last && std::isfinite(value);
  }

  [[nodiscard]] bool parsePoint(Vec3& point) {
    skipHorizontalSpace();
    if (!consume('(')) {
      return false;
    }
    if (!parseNumber(point.x) || !parseNumber(point.y) || !parseNumber(point.z)) {
      return false;
    }
    skipHorizontalSpace();
    return consume(')');
  }

  [[nodiscard]] bool parseFace(MapFace& face) {
    lastError_.clear();
    if (!parsePoint(face.points[0]) || !parsePoint(face.points[1]) || !parsePoint(face.points[2])) {
      return false;
    }
    skipHorizontalSpace();
    face.material.clear();
    while (!atEnd() && !std::isspace(static_cast<unsigned char>(peek()))) {
      if (peek() == '}') {
        break;
      }
      face.material.push_back(peek());
      advance();
    }
    const std::size_t savedOffset = offset_;
    const int savedLine = line_;
    skipHorizontalSpace();
    if (peek() == '[') {
      lastError_ = "Valve 220 texture axes are not supported yet";
      return false;
    }
    offset_ = savedOffset;
    line_ = savedLine;
    float xOffset = 0.0F;
    float yOffset = 0.0F;
    float rotation = 0.0F;
    float xScale = 1.0F;
    float yScale = 1.0F;
    if (
      parseNumber(xOffset) &&
      parseNumber(yOffset) &&
      parseNumber(rotation) &&
      parseNumber(xScale) &&
      parseNumber(yScale)
    ) {
      face.xOffset = xOffset;
      face.yOffset = yOffset;
      face.rotationDegrees = rotation;
      face.xScale = xScale;
      face.yScale = yScale;
    } else {
      offset_ = savedOffset;
      line_ = savedLine;
    }
    while (!atEnd() && peek() != '\n') {
      advance();
    }
    return true;
  }

  [[nodiscard]] MapParseResult fail(const std::string& message) const {
    return {{}, false, "line " + std::to_string(line_) + ": " + message};
  }

  std::string_view text_;
  std::size_t offset_ = 0;
  int line_ = 1;
  std::string lastError_;
};

} // namespace

MapParseResult parseMapDocument(std::string_view text) {
  return Parser(text).parse();
}

} // namespace lg
