#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 1, binding = 0, std140) uniform ShadowCameraData {
  vec4 origin;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 parameters;
} shadowCamera;

void main() {
  vec3 offset = inPosition - shadowCamera.origin.xyz;
  gl_Position = vec4(
    dot(offset, shadowCamera.right.xyz) / shadowCamera.parameters.x,
    dot(offset, shadowCamera.up.xyz) / shadowCamera.parameters.x,
    dot(offset, shadowCamera.forward.xyz) / shadowCamera.parameters.y,
    1.0
  );
}
