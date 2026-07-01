#version 450

layout(location = 0) out vec2 texCoord;

void main() {
  vec2 position;
  if (gl_VertexIndex == 0) {
    position = vec2(-1.0, -1.0);
  } else if (gl_VertexIndex == 1) {
    position = vec2(3.0, -1.0);
  } else {
    position = vec2(-1.0, 3.0);
  }
  texCoord = position * 0.5 + vec2(0.5);
  texCoord.y = 1.0 - texCoord.y;
  gl_Position = vec4(position, 0.0, 1.0);
}
