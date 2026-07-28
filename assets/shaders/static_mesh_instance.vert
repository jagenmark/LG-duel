#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 3) in vec4 instanceModelRow0;
layout(location = 4) in vec4 instanceModelRow1;
layout(location = 5) in vec4 instanceModelRow2;
layout(location = 6) in vec4 instanceColor;

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out float viewDistance;

layout(set = 1, binding = 0, std140) uniform CameraData {
  vec4 position;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 projection;
} camera;

invariant gl_Position;

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
  vec4 local = vec4(inPosition, 1.0);
  vec3 worldPosition = vec3(
    dot(instanceModelRow0, local),
    dot(instanceModelRow1, local),
    dot(instanceModelRow2, local)
  );
  gl_Position = projectWorld(worldPosition);
  vertexColor = inColor * instanceColor;
  viewDistance = max(
    dot(worldPosition - camera.position.xyz, camera.forward.xyz),
    0.0
  );
}
