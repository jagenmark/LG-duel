#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in float viewDistance;
layout(location = 4) in vec3 worldNormal;
layout(location = 5) flat in uint materialSlot;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;
layout(set = 3, binding = 0, std140) uniform SceneLightData {
  vec4 parameters;
  vec4 positionRadius[8];
  vec4 colorIntensity[8];
  vec4 sunDirectionIntensity;
  vec4 sunColor;
  vec4 fillColorIntensity;
  vec4 shadowOrigin;
  vec4 shadowRight;
  vec4 shadowUp;
  vec4 shadowForward;
  vec4 shadowParameters;
  vec4 postParameters;
} sceneLights;

vec3 directDisplay(vec3 color) {
  vec3 mapped = clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
  return pow(mapped, vec3(1.0 / 2.2));
}

void main() {
  vec4 sampled = texture(worldAtlas, texCoord) * vertexColor;
  vec3 linearColor = pow(max(sampled.rgb, vec3(0.0)), vec3(2.2));
  int materialQuality =
    clamp(int(sceneLights.parameters.w + 0.5), 0, 2);
  if (materialQuality > 0) {
    int count = clamp(int(sceneLights.parameters.x + 0.5), 0, 8);
    for (int index = 0; index < count; ++index) {
      vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
      float radius = max(sceneLights.positionRadius[index].w, 0.001);
      float distanceToLight = length(offset);
      float attenuation = clamp(1.0 - distanceToLight / radius, 0.0, 1.0);
      attenuation *= attenuation;
      linearColor += sceneLights.colorIntensity[index].rgb *
        sceneLights.colorIntensity[index].w * attenuation;
    }
  }
  int atmosphereQuality =
    clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  float hazeStart = atmosphereQuality == 1
    ? 32.0
    : atmosphereQuality == 2 ? 28.0 : 24.0;
  float hazeDensity = atmosphereQuality == 1
    ? 0.006
    : atmosphereQuality == 2 ? 0.010 : 0.014;
  float hazeCap = atmosphereQuality == 1
    ? 0.18
    : atmosphereQuality == 2 ? 0.32 : 0.42;
  float haze = atmosphereQuality == 0
    ? 0.0
    : 1.0 - exp(-max(viewDistance - hazeStart, 0.0) * hazeDensity);
  linearColor = mix(
    linearColor,
    vec3(0.0707, 0.0932, 0.1332),
    min(haze, hazeCap)
  );
  outColor = sceneLights.postParameters.x < 0.0
    ? vec4(directDisplay(linearColor), 1.0)
    : vec4(linearColor, sampled.a);
}
