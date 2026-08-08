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
layout(location = 13) in vec4 inTintMask;
layout(location = 14) in vec4 instanceAmbient;

layout(location = 0) out vec4 baseColor;
layout(location = 1) out vec4 teamTint;
layout(location = 2) out float tintWeight;
layout(location = 3) out vec3 worldPositionOut;
layout(location = 4) out vec3 worldNormalOut;
layout(location = 5) out float viewDistance;
layout(location = 6) flat out uint rimQuality;
layout(location = 7) out vec3 viewDirection;
layout(location = 8) out vec2 materialTexCoord;
layout(location = 9) out float albedoTextureMode;
layout(location = 10) out vec2 ambientData;

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

invariant gl_Position;

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

// Instance rows store an orthogonal right/up/forward player basis with
// independent horizontal and vertical scale. Rebuild the inverse-transpose
// cheaply from those columns rather than inverting a matrix for every vertex.
vec3 transformInstanceNormal(vec3 normal) {
  vec3 rightColumn = vec3(
    instanceModelRow0.x,
    instanceModelRow1.x,
    instanceModelRow2.x
  );
  vec3 upColumn = vec3(
    instanceModelRow0.y,
    instanceModelRow1.y,
    instanceModelRow2.y
  );
  vec3 forwardColumn = vec3(
    instanceModelRow0.z,
    instanceModelRow1.z,
    instanceModelRow2.z
  );
  return rightColumn * normal.x /
      max(dot(rightColumn, rightColumn), 0.00000001) +
    upColumn * normal.y / max(dot(upColumn, upColumn), 0.00000001) +
    forwardColumn * normal.z /
      max(dot(forwardColumn, forwardColumn), 0.00000001);
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
  vec3 worldNormal = normalize(transformInstanceNormal(normalize(localNormal)));
  vec3 offset = worldPosition - camera.position.xyz;
  float viewX = dot(offset, camera.right.xyz);
  float viewY = dot(offset, camera.up.xyz);
  float viewZ = dot(offset, camera.forward.xyz);
  float depthA = camera.projection.w /
    (camera.projection.w - camera.projection.z);
  float depthB = -(camera.projection.z * camera.projection.w) /
    (camera.projection.w - camera.projection.z);
  gl_Position = vec4(
    viewX * camera.projection.x / camera.projection.y,
    viewY * camera.projection.x,
    depthA * viewZ + depthB,
    viewZ
  );
  baseColor = inColor;
  teamTint = instanceColor;
  tintWeight = clamp(inTintMask.x, 0.0, 1.0);
  worldPositionOut = worldPosition;
  worldNormalOut = worldNormal;
  viewDistance = max(viewZ, 0.0);
  rimQuality = uint(clamp(int(camera.position.w + 0.5), 0, 2));
  viewDirection = camera.position.xyz - worldPosition;
  materialTexCoord = inTexCoord;
  albedoTextureMode = clamp(inTintMask.y, 0.0, 1.0);
  ambientData = instanceAmbient.xy;
}
