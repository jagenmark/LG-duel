#version 450

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec3 viewDirection;
layout(location = 2) in vec4 baseColor;
layout(location = 3) in vec2 material;
layout(location = 4) in vec3 worldPosition;
layout(location = 5) in float viewDistance;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform samplerCube weaponEnvironment;
layout(set = 2, binding = 1) uniform sampler2DShadow sunShadowMap;
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

const float PI = 3.14159265359;

vec3 directDisplay(vec3 color) {
  vec3 mapped = clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
  return pow(mapped, vec3(1.0 / 2.2));
}

float sunShadowVisibility(vec3 position, vec3 normal) {
  if (
    sceneLights.postParameters.w >= 0.5 ||
    sceneLights.shadowParameters.z < 1.0
  ) {
    return 1.0;
  }
  vec3 receiver = position + normalize(normal) * sceneLights.shadowOrigin.w;
  vec3 offset = receiver - sceneLights.shadowOrigin.xyz;
  float extent = max(sceneLights.shadowParameters.x, 0.001);
  vec2 uv = vec2(
    dot(offset, sceneLights.shadowRight.xyz) / extent,
    -dot(offset, sceneLights.shadowUp.xyz) / extent
  ) * 0.5 + 0.5;
  float receiverDepth =
    dot(offset, sceneLights.shadowForward.xyz) /
      max(sceneLights.shadowParameters.y, 0.001) -
    sceneLights.shadowParameters.w;
  if (
    receiverDepth <= 0.0 || receiverDepth >= 1.0 ||
    any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))
  ) {
    return 1.0;
  }
  return texture(sunShadowMap, vec3(uv, receiverDepth));
}

vec3 directSpecular(
  vec3 n,
  vec3 v,
  vec3 l,
  vec3 albedo,
  float metallic,
  float roughness
) {
  vec3 h = normalize(v + l);
  float nDotL = max(dot(n, l), 0.0);
  float nDotV = max(dot(n, v), 0.001);
  float nDotH = max(dot(n, h), 0.0);
  float vDotH = max(dot(v, h), 0.0);
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
  float distribution =
    alpha2 / max(PI * denominator * denominator, 0.001);
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float geometryV = nDotV / (nDotV * (1.0 - k) + k);
  float geometryL = nDotL / (nDotL * (1.0 - k) + k);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
  return distribution * geometryV * geometryL * fresnel /
    max(4.0 * nDotV * nDotL, 0.001);
}

void main() {
  vec3 n = normalize(worldNormal);
  float metallic = clamp(material.x, 0.0, 1.0);
  vec3 albedo = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));
  int materialQuality =
    clamp(int(sceneLights.parameters.w + 0.5), 0, 2);

  vec3 fillRadiance = sceneLights.fillColorIntensity.rgb *
    max(sceneLights.fillColorIntensity.w, 0.0);
  float skyFill = n.z * 0.5 + 0.5;
  vec3 color = albedo * (vec3(0.16) + fillRadiance *
    (0.35 + 0.65 * skyFill));

  vec3 sunDirection = normalize(-sceneLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  float sunVisibility = sunShadowVisibility(worldPosition, n);
  vec3 sunRadiance = sceneLights.sunColor.rgb *
    max(sceneLights.sunDirectionIntensity.w, 0.0);
  color += albedo * sunRadiance * sunNDotL *
    (1.0 - metallic * 0.72) * sunVisibility;
  if (materialQuality > 0) {
    vec3 v = normalize(viewDirection);
    float roughness = clamp(material.y, 0.10, 1.0);
    if (sunNDotL > 0.0) {
      float qualityScale = materialQuality == 1 ? 0.55 : 1.0;
      color += directSpecular(
        n, v, sunDirection, albedo, metallic, roughness
      ) * sunRadiance * sunNDotL * sunVisibility * qualityScale;
    }

    int lightCount = clamp(int(sceneLights.parameters.x + 0.5), 0, 8);
    for (int index = 0; index < lightCount; ++index) {
      vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
      float radius = max(sceneLights.positionRadius[index].w, 0.001);
      float distanceToLight = length(offset);
      vec3 lightDirection = offset / max(distanceToLight, 0.001);
      float attenuation = clamp(1.0 - distanceToLight / radius, 0.0, 1.0);
      attenuation *= attenuation;
      float nDotL = max(dot(n, lightDirection), 0.0);
      vec3 radiance = sceneLights.colorIntensity[index].rgb *
        sceneLights.colorIntensity[index].w * attenuation;
      color += albedo * radiance * nDotL * (1.0 - metallic * 0.72);
      if (materialQuality == 2 && nDotL > 0.0) {
        color += directSpecular(
          n, v, lightDirection, albedo, metallic, roughness
        ) * radiance * nDotL;
      }
    }

    vec3 reflected = reflect(-v, n);
    vec3 environment =
      textureLod(weaponEnvironment, reflected, roughness * 6.0).rgb;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    color += environment * f0 * mix(0.70, 0.28, roughness) *
      (materialQuality == 1 ? 0.60 : 1.0);
  }

  int atmosphereQuality =
    clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  if (atmosphereQuality > 0 && sceneLights.postParameters.w < 0.5) {
    float hazeStart = atmosphereQuality == 1
      ? 32.0
      : atmosphereQuality == 2 ? 28.0 : 24.0;
    float hazeDensity = atmosphereQuality == 1
      ? 0.006
      : atmosphereQuality == 2 ? 0.010 : 0.014;
    float hazeCap = atmosphereQuality == 1
      ? 0.18
      : atmosphereQuality == 2 ? 0.32 : 0.42;
    float haze = min(
      1.0 - exp(-max(viewDistance - hazeStart, 0.0) * hazeDensity),
      hazeCap
    );
    color = mix(color, vec3(0.0707, 0.0932, 0.1332), haze);
  }
  outColor = sceneLights.postParameters.x < 0.0
    ? vec4(directDisplay(color), 1.0)
    : vec4(color, baseColor.a);
}
