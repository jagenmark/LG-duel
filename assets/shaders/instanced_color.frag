#version 450
#extension GL_EXT_texture_shadow_lod : require

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in float viewDistance;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in vec3 worldNormal;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2DArrayShadow pointShadowMap;
layout(set = 3, binding = 0, std140) uniform SceneLightData {
  vec4 parameters;
  vec4 positionRadius[32];
  vec4 colorIntensity[32];
  vec4 lightParameters[32];
  vec4 pointShadowParameters;
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

float pointShadowVisibility(int index, vec3 position, vec3 normal) {
  int shadowSlot = int(sceneLights.lightParameters[index].z + 0.5) - 1;
  if (shadowSlot < 0 || sceneLights.pointShadowParameters.x < 1.0) {
    return 1.0;
  }
  float radius = max(sceneLights.positionRadius[index].w, 0.05);
  float sourceRadius = clamp(sceneLights.lightParameters[index].x, 0.0, radius);
  vec3 receiver = position + normalize(normal) *
    max(0.012, sourceRadius * 0.12);
  vec3 direction = receiver - sceneLights.positionRadius[index].xyz;
  vec3 a = abs(direction);
  float major = max(a.x, max(a.y, a.z));
  if (major <= 0.001 || major >= radius) {
    return 1.0;
  }
  int face;
  vec2 projected;
  if (a.x >= a.y && a.x >= a.z) {
    if (direction.x >= 0.0) {
      face = 0; projected = vec2(-direction.y, -direction.z);
    } else {
      face = 1; projected = vec2(direction.y, -direction.z);
    }
  } else if (a.y >= a.z) {
    if (direction.y >= 0.0) {
      face = 2; projected = vec2(direction.x, -direction.z);
    } else {
      face = 3; projected = vec2(-direction.x, -direction.z);
    }
  } else if (direction.z >= 0.0) {
    face = 4; projected = vec2(direction.x, -direction.y);
  } else {
    face = 5; projected = vec2(-direction.x, -direction.y);
  }
  vec2 uv = projected / major * 0.5 + 0.5;
  float nearPlane = clamp(
    max(0.025, sourceRadius * 0.25),
    0.025,
    max(0.025, radius * 0.25)
  );
  float receiverDepth =
    radius / max(radius - nearPlane, 0.001) -
    (nearPlane * radius) /
      max(radius - nearPlane, 0.001) / major -
    sceneLights.pointShadowParameters.z -
    (sourceRadius / radius) * 0.0015;
  float layer = float(shadowSlot * 6 + face);
  float mapSize = sceneLights.pointShadowParameters.x;
  float softness = clamp(sourceRadius / radius, 0.0, 1.0);
  if (softness <= 0.0001) {
    return textureLod(pointShadowMap, vec4(uv, layer, receiverDepth), 0.0);
  }
  float kernelTexels = mapSize < 512.0
    ? min(0.5 + softness * 0.75, 1.0)
    : min(0.65 + softness * 1.35, 2.0);
  vec2 texel = vec2(kernelTexels / mapSize);
  float visibility = 0.0;
  visibility += textureLod(
    pointShadowMap,
    vec4(uv + vec2(-texel.x, -texel.y), layer, receiverDepth),
    0.0
  );
  visibility += textureLod(
    pointShadowMap,
    vec4(uv + vec2(texel.x, -texel.y), layer, receiverDepth),
    0.0
  );
  visibility += textureLod(
    pointShadowMap,
    vec4(uv + vec2(-texel.x, texel.y), layer, receiverDepth),
    0.0
  );
  visibility += textureLod(
    pointShadowMap,
    vec4(uv + texel, layer, receiverDepth),
    0.0
  );
  return visibility * 0.25;
}

void main() {
  vec3 albedo = pow(max(vertexColor.rgb, vec3(0.0)), vec3(2.2));
  vec3 color = albedo;
  vec3 n = normalize(worldNormal);
  int lightCount = clamp(int(sceneLights.parameters.x + 0.5), 0, 32);
  for (int index = 0; index < lightCount; ++index) {
    vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
    float radius = max(sceneLights.positionRadius[index].w, 0.001);
    float lightDistance = length(offset);
    if (lightDistance >= radius) {
      continue;
    }
    float sourceRadius = clamp(
      sceneLights.lightParameters[index].x,
      0.0,
      radius
    );
    float attenuation = clamp(
      (radius - lightDistance) / max(radius - sourceRadius, 0.001),
      0.0,
      1.0
    );
    attenuation *= attenuation;
    float nDotL = max(dot(n, offset / max(lightDistance, 0.001)), 0.0);
    if (nDotL <= 0.0) {
      continue;
    }
    vec3 radiance = sceneLights.colorIntensity[index].rgb *
      sceneLights.colorIntensity[index].w * attenuation *
      sceneLights.lightParameters[index].w *
      pointShadowVisibility(index, worldPosition, n);
    color += albedo * radiance * nDotL;
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
    : vec4(color, vertexColor.a);
}
