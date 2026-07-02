#version 450

void main() {
  vec2 position;
  if (gl_VertexIndex == 0) {
    position = vec2(-1.0, -1.0);
  } else if (gl_VertexIndex == 1) {
    position = vec2(3.0, -1.0);
  } else {
    position = vec2(-1.0, 3.0);
  }
  gl_Position = vec4(position, 1.0, 1.0);
}
