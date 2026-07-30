#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 1, binding = 0, std140) uniform PointShadowCameraData {
  vec4 origin;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 parameters;
} shadowCamera;

void main() {
  vec3 offset = inPosition - shadowCamera.origin.xyz;
  float viewX = dot(offset, shadowCamera.right.xyz);
  float viewY = dot(offset, shadowCamera.up.xyz);
  float viewZ = dot(offset, shadowCamera.forward.xyz);
  float nearPlane = shadowCamera.parameters.x;
  float farPlane = shadowCamera.parameters.y;
  float depthA = farPlane / max(farPlane - nearPlane, 0.001);
  float depthB = -(nearPlane * farPlane) /
    max(farPlane - nearPlane, 0.001);
  gl_Position = vec4(
    viewX,
    viewY,
    depthA * viewZ + depthB,
    viewZ
  );
}
