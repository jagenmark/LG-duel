#pragma once

#include "sim/Arena.hpp"
#include "sim/PlayerState.hpp"

namespace lg {

class Renderer {
public:
  Renderer() = default;
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  ~Renderer();

  [[nodiscard]] bool initialize(void* window);
  void render(const Arena& arena, const PlayerState& player);
  void shutdown();

private:
  void* renderer_ = nullptr;
};

} // namespace lg
