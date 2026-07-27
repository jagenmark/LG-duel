#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 worldPosition;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;
layout(set = 3, binding = 0, std140) uniform CombatLightData {
  vec4 parameters;
  vec4 positionRadius[8];
  vec4 colorIntensity[8];
} combatLights;

vec3 filmicToneMap(vec3 color) {
  return clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
}

void main() {
  vec4 sampled = texture(worldAtlas, texCoord) * vertexColor;
  vec3 linearColor = pow(max(sampled.rgb, vec3(0.0)), vec3(2.2));
  int count = clamp(int(combatLights.parameters.x + 0.5), 0, 8);
  for (int index = 0; index < count; ++index) {
    vec3 offset = combatLights.positionRadius[index].xyz - worldPosition;
    float radius = max(combatLights.positionRadius[index].w, 0.001);
    float distanceToLight = length(offset);
    float attenuation = clamp(1.0 - distanceToLight / radius, 0.0, 1.0);
    attenuation *= attenuation;
    linearColor += combatLights.colorIntensity[index].rgb *
      combatLights.colorIntensity[index].w * attenuation;
  }
  vec3 mapped = filmicToneMap(
    linearColor * max(combatLights.parameters.y, 0.01)
  );
  outColor = vec4(pow(mapped, vec3(1.0 / 2.2)), sampled.a);
}
