#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in float viewDistance;
layout(location = 4) in vec3 worldNormal;
layout(location = 5) flat in uint materialSlot;
layout(location = 6) in vec3 viewDirection;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;
layout(set = 2, binding = 1) uniform sampler2DShadow sunShadowMap;
layout(set = 2, binding = 2) uniform sampler2DArrayShadow pointShadowMap;
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
layout(set = 3, binding = 1, std140) uniform WorldMaterialData {
  vec4 traits;
} worldMaterial;

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
  float mapSize = sceneLights.shadowParameters.z;
  if (mapSize < 1.0) {
    return 1.0;
  }
  vec3 receiver = position +
    normalize(normal) * sceneLights.shadowOrigin.w;
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
  float visibility = 0.0;
  if (mapSize >= 2048.0) {
    vec2 texel = vec2(1.0 / mapSize);
    for (int y = -1; y <= 1; ++y) {
      for (int x = -1; x <= 1; ++x) {
        visibility += texture(
          sunShadowMap,
          vec3(uv + vec2(x, y) * texel, receiverDepth)
        );
      }
    }
    visibility /= 9.0;
  } else {
    visibility = texture(sunShadowMap, vec3(uv, receiverDepth));
  }
  float edge = smoothstep(
    0.0,
    0.035,
    min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y))
  );
  return mix(1.0, visibility, edge);
}

float pointShadowVisibility(int index, vec3 position, vec3 normal) {
  int shadowSlot = int(sceneLights.lightParameters[index].z + 0.5) - 1;
  if (
    shadowSlot < 0 ||
    sceneLights.pointShadowParameters.x < 1.0
  ) {
    return 1.0;
  }
  float radius = max(sceneLights.positionRadius[index].w, 0.05);
  float sourceRadius = clamp(
    sceneLights.lightParameters[index].x,
    0.0,
    radius
  );
  vec3 receiver = position + normalize(normal) *
    max(0.012, sourceRadius * 0.12);
  vec3 direction = receiver - sceneLights.positionRadius[index].xyz;
  vec3 absoluteDirection = abs(direction);
  float major = max(
    absoluteDirection.x,
    max(absoluteDirection.y, absoluteDirection.z)
  );
  if (major <= 0.001 || major >= radius) {
    return 1.0;
  }
  int face = 0;
  vec2 projected = vec2(0.0);
  if (
    absoluteDirection.x >= absoluteDirection.y &&
    absoluteDirection.x >= absoluteDirection.z
  ) {
    if (direction.x >= 0.0) {
      face = 0;
      projected = vec2(-direction.y, -direction.z);
    } else {
      face = 1;
      projected = vec2(direction.y, -direction.z);
    }
  } else if (absoluteDirection.y >= absoluteDirection.z) {
    if (direction.y >= 0.0) {
      face = 2;
      projected = vec2(direction.x, -direction.z);
    } else {
      face = 3;
      projected = vec2(-direction.x, -direction.z);
    }
  } else if (direction.z >= 0.0) {
    face = 4;
    projected = vec2(direction.x, -direction.y);
  } else {
    face = 5;
    projected = vec2(-direction.x, -direction.y);
  }
  vec2 uv = projected / major * 0.5 + 0.5;
  float nearPlane = clamp(
    max(0.025, sourceRadius * 0.25),
    0.025,
    max(0.025, radius * 0.25)
  );
  float depthA = radius / max(radius - nearPlane, 0.001);
  float depthB = -(nearPlane * radius) /
    max(radius - nearPlane, 0.001);
  float receiverDepth = depthA + depthB / major;
  receiverDepth -= sceneLights.pointShadowParameters.z +
    (sourceRadius / radius) * 0.0015;
  float layer = float(shadowSlot * 6 + face);
  float mapSize = sceneLights.pointShadowParameters.x;
  float softness = clamp(sourceRadius / radius, 0.0, 1.0);
  if (softness <= 0.0001) {
    return texture(pointShadowMap, vec4(uv, layer, receiverDepth));
  }
  float kernelTexels = mapSize < 512.0
    ? min(0.5 + softness * 0.75, 1.0)
    : min(0.65 + softness * 1.35, 2.0);
  vec2 texel = vec2(kernelTexels / mapSize);
  float visibility = 0.0;
  visibility += texture(
    pointShadowMap,
    vec4(uv + vec2(-texel.x, -texel.y), layer, receiverDepth)
  );
  visibility += texture(
    pointShadowMap,
    vec4(uv + vec2(texel.x, -texel.y), layer, receiverDepth)
  );
  visibility += texture(
    pointShadowMap,
    vec4(uv + vec2(-texel.x, texel.y), layer, receiverDepth)
  );
  visibility += texture(
    pointShadowMap,
    vec4(uv + texel, layer, receiverDepth)
  );
  return visibility * 0.25;
}

void main() {
  vec4 sampled = texture(worldAtlas, texCoord);
  vec3 albedo = pow(max(sampled.rgb, vec3(0.0)), vec3(2.2));
  int materialQuality =
    clamp(int(sceneLights.parameters.w + 0.5), 0, 2);

  vec3 bakedLight = max(vertexColor.rgb, vec3(0.00169355));
  vec3 sceneColor = albedo * bakedLight;
  float bakedPeak = max(
    max(vertexColor.r, vertexColor.g),
    max(vertexColor.b, 0.001)
  );
  vec3 authoredTint = clamp(vertexColor.rgb / bakedPeak, 0.0, 1.0);
  vec3 directAlbedo = albedo * authoredTint;
  vec3 n = normalize(worldNormal);
  vec3 sunDirection = normalize(-sceneLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  float sunVisibility = sunShadowVisibility(worldPosition, n);
  vec3 sunRadiance = sceneLights.sunColor.rgb *
    max(sceneLights.sunDirectionIntensity.w, 0.0);
  sceneColor += directAlbedo * sunRadiance * sunNDotL * sunVisibility;

  if (materialQuality > 0) {
    vec3 v = normalize(viewDirection);
    float roughness = clamp(worldMaterial.traits.x, 0.10, 1.0);
    float metallic = clamp(worldMaterial.traits.y, 0.0, 1.0);
    float specularStrength = clamp(worldMaterial.traits.z, 0.0, 1.0);
    if (sunNDotL > 0.0) {
      vec3 halfDirection = normalize(sunDirection + v);
      float exponent = mix(10.0, 72.0, 1.0 - roughness);
      float highlight = pow(max(dot(n, halfDirection), 0.0), exponent);
      float qualityScale = materialQuality == 1 ? 0.55 : 1.0;
      vec3 specularColor = mix(vec3(0.045), directAlbedo, metallic);
      sceneColor += specularColor * sunRadiance * highlight *
        sunNDotL * specularStrength * qualityScale * sunVisibility;
    }

    int lightCount = clamp(int(sceneLights.parameters.x + 0.5), 0, 32);
    for (int index = 0; index < lightCount; ++index) {
      vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
      float radius = max(sceneLights.positionRadius[index].w, 0.001);
      float lightDistance = length(offset);
      vec3 lightDirection = offset / max(lightDistance, 0.001);
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
      float localNDotL = max(dot(n, lightDirection), 0.0);
      float liveWorldScale = sceneLights.lightParameters[index].y *
        sceneLights.lightParameters[index].w;
      float pointVisibility = pointShadowVisibility(
        index,
        worldPosition,
        n
      );
      vec3 radiance = sceneLights.colorIntensity[index].rgb *
        sceneLights.colorIntensity[index].w * attenuation * liveWorldScale *
        pointVisibility;
      sceneColor += directAlbedo * radiance * localNDotL;
      if (materialQuality == 2 && localNDotL > 0.0) {
        vec3 halfDirection = normalize(lightDirection + v);
        float exponent = mix(10.0, 72.0, 1.0 - roughness);
        float highlight = pow(max(dot(n, halfDirection), 0.0), exponent);
        vec3 specularColor = mix(vec3(0.045), directAlbedo, metallic);
        sceneColor += specularColor * radiance * highlight *
          localNDotL * specularStrength;
      }
    }
  }
  // Emissive tags stay depth-correct in the opaque pass. They do not enter
  // the selective bloom source pass.
  sceneColor += albedo * clamp(worldMaterial.traits.w, 0.0, 0.35);
  int atmosphereQuality =
    clamp(int(sceneLights.parameters.z + 0.5), 0, 3);

  // Keep near combat crisp. Only the far half of large maps gets a restrained
  // blue-grey haze, capped so silhouettes stay readable.
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
  haze = min(haze, hazeCap);
  const vec3 hazeColor = vec3(0.0707, 0.0932, 0.1332);
  sceneColor = mix(sceneColor, hazeColor, haze);
  if (sceneLights.postParameters.x < 0.0) {
    outColor = vec4(directDisplay(sceneColor), 1.0);
  } else {
    // Keep this output linear for the same single scene-to-display curve.
    outColor = vec4(sceneColor, sampled.a * vertexColor.a);
  }
}
