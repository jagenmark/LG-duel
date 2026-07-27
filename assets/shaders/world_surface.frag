#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in float viewDistance;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;
layout(set = 3, binding = 0, std140) uniform CombatLightData {
  vec4 parameters;
  vec4 positionRadius[8];
  vec4 colorIntensity[8];
} combatLights;

vec3 acesToneMap(vec3 color) {
  return clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
}

vec3 gradeWorld(vec3 displayColor) {
  // Keep the restrained cool grade after the scene curve has restored the
  // baked low-light range.
  vec3 color =
    displayColor * vec3(1.035, 1.015, 0.985) +
    vec3(0.006, 0.008, 0.012);
  float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
  return clamp(mix(vec3(luminance), color, 1.04), 0.0, 1.0);
}

void main() {
  vec4 surface = texture(worldAtlas, texCoord) * vertexColor;
  // Static vertices contain baked light, but their texture product still
  // needs the same single scene-to-display curve used before the global pass.
  // Sending that product out almost unchanged caused low values to stay near
  // black after PR #230.
  vec3 sceneColor = pow(max(surface.rgb, vec3(0.0)), vec3(2.2));
  int lightCount = clamp(int(combatLights.parameters.x + 0.5), 0, 8);
  for (int index = 0; index < lightCount; ++index) {
    vec3 offset = combatLights.positionRadius[index].xyz - worldPosition;
    float radius = max(combatLights.positionRadius[index].w, 0.001);
    float lightDistance = length(offset);
    float attenuation = clamp(1.0 - lightDistance / radius, 0.0, 1.0);
    attenuation *= attenuation;
    sceneColor += combatLights.colorIntensity[index].rgb *
      combatLights.colorIntensity[index].w * attenuation;
  }
  vec3 displayColor = pow(
    acesToneMap(
      sceneColor * max(combatLights.parameters.y, 0.01)
    ),
    vec3(1.0 / 2.2)
  );
  vec3 color = gradeWorld(displayColor);

  // Keep near combat crisp. Only the far half of large maps gets a restrained
  // blue-grey haze, capped so silhouettes stay readable.
  float haze = 1.0 - exp(-max(viewDistance - 28.0, 0.0) * 0.010);
  haze = min(haze, 0.32);
  const vec3 hazeColor = vec3(0.30, 0.34, 0.40);
  color = mix(color, hazeColor, haze);
  outColor = vec4(color, surface.a);
}
