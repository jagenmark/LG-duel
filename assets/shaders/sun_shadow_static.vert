#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec4 instanceModelRow0;
layout(location = 4) in vec4 instanceModelRow1;
layout(location = 5) in vec4 instanceModelRow2;

layout(set = 1, binding = 0, std140) uniform ShadowCameraData {
  vec4 origin;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 parameters;
} shadowCamera;

void main() {
  vec4 local = vec4(inPosition, 1.0);
  vec3 position = vec3(
    dot(instanceModelRow0, local),
    dot(instanceModelRow1, local),
    dot(instanceModelRow2, local)
  );
  vec3 offset = position - shadowCamera.origin.xyz;
  gl_Position = vec4(
    dot(offset, shadowCamera.right.xyz) / shadowCamera.parameters.x,
    dot(offset, shadowCamera.up.xyz) / shadowCamera.parameters.x,
    dot(offset, shadowCamera.forward.xyz) / shadowCamera.parameters.y,
    1.0
  );
}
