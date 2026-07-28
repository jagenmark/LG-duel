#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 3) in vec3 instancePosition;
layout(location = 4) in vec3 instanceScale;
layout(location = 5) in float instanceRotationRadians;
layout(location = 6) in float instancePitchRadians;
layout(location = 7) in vec4 instanceColor;
layout(location = 8) in float instancePhase;

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec2 quadUv;
layout(location = 2) out float viewDistance;

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
  float pulse = 1.0 + 0.045 * sin(instancePhase * 6.28318530718);
  vec2 local = inPosition.xy;
  vec3 worldPosition =
    instancePosition +
    camera.right.xyz * (local.x * instanceScale.x * pulse) +
    camera.up.xyz * (local.y * instanceScale.y * pulse);
  gl_Position = projectWorld(worldPosition);
  vertexColor = inColor * instanceColor;
  quadUv = inTexCoord;
  viewDistance = max(
    dot(worldPosition - camera.position.xyz, camera.forward.xyz),
    0.0
  );
}
