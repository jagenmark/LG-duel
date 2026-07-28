#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in vec4 instanceModelRow0;
layout(location = 7) in vec4 instanceModelRow1;
layout(location = 8) in vec4 instanceModelRow2;
layout(location = 10) in uint instanceFirstBone;
layout(location = 11) in uint instanceBoneCount;
layout(location = 12) in uint instanceFlags;

layout(set = 0, binding = 0, std430) readonly buffer BoneRows {
  vec4 rows[];
} bones;

layout(set = 1, binding = 0, std140) uniform ShadowCameraData {
  vec4 origin;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 parameters;
} shadowCamera;

vec3 transformBonePoint(uint bone, vec3 point) {
  uint row = (instanceFirstBone + bone) * 4u;
  vec4 local = vec4(point, 1.0);
  return vec3(
    dot(bones.rows[row + 0u], local),
    dot(bones.rows[row + 1u], local),
    dot(bones.rows[row + 2u], local)
  );
}

void main() {
  vec3 localPosition = inPosition;
  bool useSkin = (instanceFlags & 1u) != 0u && instanceBoneCount > 0u;
  if (useSkin) {
    vec3 skinnedPosition = vec3(0.0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < 4u; ++i) {
      float weight = max(inWeights[i], 0.0);
      uint joint = inJoints[i];
      if (weight <= 0.000001 || joint >= instanceBoneCount) {
        continue;
      }
      skinnedPosition += transformBonePoint(joint, inPosition) * weight;
      totalWeight += weight;
    }
    if (totalWeight > 0.000001) {
      localPosition = skinnedPosition / totalWeight;
    }
  }
  vec4 local = vec4(localPosition, 1.0);
  vec3 worldPosition = vec3(
    dot(instanceModelRow0, local),
    dot(instanceModelRow1, local),
    dot(instanceModelRow2, local)
  );
  vec3 offset = worldPosition - shadowCamera.origin.xyz;
  gl_Position = vec4(
    dot(offset, shadowCamera.right.xyz) / shadowCamera.parameters.x,
    dot(offset, shadowCamera.up.xyz) / shadowCamera.parameters.x,
    dot(offset, shadowCamera.forward.xyz) / shadowCamera.parameters.y,
    1.0
  );
}
