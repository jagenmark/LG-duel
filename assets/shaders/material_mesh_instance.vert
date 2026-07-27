#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inBaseColor;
layout(location = 3) in vec2 inMaterial;
layout(location = 4) in vec4 instanceModelRow0;
layout(location = 5) in vec4 instanceModelRow1;
layout(location = 6) in vec4 instanceModelRow2;
layout(location = 7) in vec4 instanceColor;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec3 viewDirection;
layout(location = 2) out vec4 baseColor;
layout(location = 3) out vec2 material;
layout(location = 4) out vec3 worldPositionOut;

layout(set = 1, binding = 0, std140) uniform CameraData {
  vec4 position;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 projection;
} camera;

void main() {
  vec4 local = vec4(inPosition, 1.0);
  vec3 worldPosition = vec3(
    dot(instanceModelRow0, local),
    dot(instanceModelRow1, local),
    dot(instanceModelRow2, local)
  );
  vec3 offset = worldPosition - camera.position.xyz;
  float viewX = dot(offset, camera.right.xyz);
  float viewY = dot(offset, camera.up.xyz);
  float viewZ = dot(offset, camera.forward.xyz);
  float depthA = camera.projection.w / (camera.projection.w - camera.projection.z);
  float depthB = -(camera.projection.z * camera.projection.w) /
    (camera.projection.w - camera.projection.z);
  gl_Position = vec4(
    viewX * camera.projection.x / camera.projection.y,
    viewY * camera.projection.x,
    depthA * viewZ + depthB,
    viewZ
  );

  worldNormal = normalize(vec3(
    dot(instanceModelRow0.xyz, inNormal),
    dot(instanceModelRow1.xyz, inNormal),
    dot(instanceModelRow2.xyz, inNormal)
  ));
  viewDirection = normalize(camera.position.xyz - worldPosition);
  baseColor = inBaseColor * instanceColor;
  material = inMaterial;
  worldPositionOut = worldPosition;
}
