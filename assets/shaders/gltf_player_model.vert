#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

layout(location = 6) in vec4 instanceModelRow0;
layout(location = 7) in vec4 instanceModelRow1;
layout(location = 8) in vec4 instanceModelRow2;
layout(location = 9) in vec4 instanceColor;
layout(location = 10) in uint instanceFirstBone;
layout(location = 11) in uint instanceBoneCount;
layout(location = 12) in uint instanceFlags;

layout(location = 0) out vec4 vertexColor;

layout(set = 0, binding = 0, std430) readonly buffer BoneRows {
  vec4 rows[];
} bones;

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

vec3 transformBonePoint(uint bone, vec3 point) {
  uint row = (instanceFirstBone + bone) * 4u;
  vec4 local = vec4(point, 1.0);
  return vec3(
    dot(bones.rows[row + 0u], local),
    dot(bones.rows[row + 1u], local),
    dot(bones.rows[row + 2u], local)
  );
}

vec3 transformBoneNormal(uint bone, vec3 normal) {
  uint row = (instanceFirstBone + bone) * 4u;
  vec4 local = vec4(normal, 0.0);
  return vec3(
    dot(bones.rows[row + 0u], local),
    dot(bones.rows[row + 1u], local),
    dot(bones.rows[row + 2u], local)
  );
}

void main() {
  bool useSkin = (instanceFlags & 1u) != 0u && instanceBoneCount > 0u;
  vec3 localPosition = inPosition;
  vec3 localNormal = inNormal;
  if (useSkin) {
    vec3 skinnedPosition = vec3(0.0);
    vec3 skinnedNormal = vec3(0.0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < 4u; ++i) {
      float weight = max(inWeights[i], 0.0);
      uint joint = inJoints[i];
      if (weight <= 0.000001 || joint >= instanceBoneCount) {
        continue;
      }
      skinnedPosition += transformBonePoint(joint, inPosition) * weight;
      skinnedNormal += transformBoneNormal(joint, inNormal) * weight;
      totalWeight += weight;
    }
    if (totalWeight > 0.000001) {
      localPosition = skinnedPosition / totalWeight;
      localNormal = skinnedNormal / totalWeight;
    }
  }

  vec4 local = vec4(localPosition, 1.0);
  vec3 worldPosition = vec3(
    dot(instanceModelRow0, local),
    dot(instanceModelRow1, local),
    dot(instanceModelRow2, local)
  );
  vec4 normalLocal = vec4(normalize(localNormal), 0.0);
  vec3 worldNormal = normalize(vec3(
    dot(instanceModelRow0, normalLocal),
    dot(instanceModelRow1, normalLocal),
    dot(instanceModelRow2, normalLocal)
  ));
  vec3 lightDirection = normalize(vec3(-0.35, -0.45, 0.82));
  float brightness = clamp(
    0.70 + max(0.0, dot(worldNormal, lightDirection)) * 0.38 +
      abs(worldNormal.z) * 0.12,
    0.62,
    1.22
  );
  gl_Position = projectWorld(worldPosition);
  vertexColor = vec4(inColor.rgb * brightness, inColor.a) * instanceColor;
}
