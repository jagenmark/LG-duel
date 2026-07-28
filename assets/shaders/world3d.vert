#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in uint inMaterialSlot;

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec2 texCoord;
layout(location = 2) out vec3 worldPosition;
layout(location = 3) out float viewDistance;
layout(location = 4) out vec3 worldNormal;
layout(location = 5) flat out uint materialSlot;
layout(location = 6) out vec3 viewDirection;

layout(set = 1, binding = 0, std140) uniform CameraData {
  vec4 position;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 projection;
} camera;

invariant gl_Position;

void main() {
  vec3 offset = inPosition - camera.position.xyz;
  float viewX = dot(offset, camera.right.xyz);
  float viewY = dot(offset, camera.up.xyz);
  float viewZ = dot(offset, camera.forward.xyz);
  float focalLength = camera.projection.x;
  float aspectRatio = camera.projection.y;
  float nearPlane = camera.projection.z;
  float farPlane = camera.projection.w;
  float depthA = farPlane / (farPlane - nearPlane);
  float depthB = -(nearPlane * farPlane) / (farPlane - nearPlane);

  gl_Position = vec4(
    viewX * focalLength / aspectRatio,
    viewY * focalLength,
    depthA * viewZ + depthB,
    viewZ
  );
  vertexColor = inColor;
  texCoord = inTexCoord;
  worldPosition = inPosition;
  viewDistance = max(viewZ, 0.0);
  worldNormal = inNormal;
  materialSlot = inMaterialSlot;
  viewDirection = camera.position.xyz - inPosition;
}
