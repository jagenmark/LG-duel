#version 450

layout(location = 0) out vec3 rayDirection;

layout(set = 1, binding = 0, std140) uniform SkyCameraData {
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 projection;
} camera;

void main() {
  vec2 position;
  if (gl_VertexIndex == 0) {
    position = vec2(-1.0, -1.0);
  } else if (gl_VertexIndex == 1) {
    position = vec2(3.0, -1.0);
  } else {
    position = vec2(-1.0, 3.0);
  }
  rayDirection =
    camera.forward.xyz +
    camera.right.xyz *
      (position.x * camera.projection.y / camera.projection.x) +
    camera.up.xyz * (position.y / camera.projection.x);
  gl_Position = vec4(position, 0.0, 1.0);
}
