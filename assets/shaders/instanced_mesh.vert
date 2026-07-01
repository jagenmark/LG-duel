#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 3) in vec3 instancePosition;
layout(location = 4) in vec3 instanceScale;
layout(location = 5) in float instanceRotationRadians;
layout(location = 6) in vec4 instanceColor;
layout(location = 7) in float instancePhase;

layout(location = 0) out vec4 vertexColor;

layout(set = 1, binding = 0, std140) uniform CameraData {
  vec4 position;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 projection;
} camera;

vec4 projectWorld(vec3 worldPosition) {
  vec3 offset = worldPosition - camera.position.xyz;
  float viewX = dot(offset, camera.right.xyz);
  float viewY = dot(offset, camera.up.xyz);
  float viewZ = dot(offset, camera.forward.xyz);
  float focalLength = camera.projection.x;
  float aspectRatio = camera.projection.y;
  float nearPlane = camera.projection.z;
  float farPlane = camera.projection.w;
  float depthA = farPlane / (farPlane - nearPlane);
  float depthB = -(nearPlane * farPlane) / (farPlane - nearPlane);
  return vec4(
    viewX * focalLength / aspectRatio,
    viewY * focalLength,
    depthA * viewZ + depthB,
    viewZ
  );
}

void main() {
  float c = cos(instanceRotationRadians);
  float s = sin(instanceRotationRadians);
  vec3 scaled = inPosition * instanceScale;
  vec3 rotated = vec3(
    scaled.x * c - scaled.y * s,
    scaled.x * s + scaled.y * c,
    scaled.z
  );
  gl_Position = projectWorld(instancePosition + rotated);
  vertexColor = inColor * instanceColor;
}
