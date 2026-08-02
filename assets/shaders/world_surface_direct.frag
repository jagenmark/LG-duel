#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 4) in vec3 worldNormal;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;
layout(set = 3, binding = 0, std140) uniform DirectSunData {
  vec4 sunDirectionIntensity;
  vec4 sunColor;
} directLights;
layout(set = 3, binding = 1, std140) uniform WorldMaterialData {
  vec4 traits;
} worldMaterial;

#include "includes/direct_display.glsl"

void main() {
  vec3 albedo = pow(
    max(texture(worldAtlas, texCoord).rgb, vec3(0.0)),
    vec3(2.2)
  );
  vec3 bakedLight = max(vertexColor.rgb, vec3(0.00169355));
  vec3 sceneColor = albedo * bakedLight;
  float bakedPeak = max(
    max(vertexColor.r, vertexColor.g),
    max(vertexColor.b, 0.001)
  );
  vec3 authoredTint = clamp(vertexColor.rgb / bakedPeak, 0.0, 1.0);
  vec3 directAlbedo = albedo * authoredTint;
  vec3 n = normalize(worldNormal);
  vec3 sunDirection =
    normalize(-directLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  vec3 sunRadiance = directLights.sunColor.rgb *
    max(directLights.sunDirectionIntensity.w, 0.0);
  sceneColor += directAlbedo * sunRadiance * sunNDotL;
  sceneColor += albedo * clamp(worldMaterial.traits.w, 0.0, 0.35);
  outColor = vec4(directDisplay(sceneColor), 1.0);
}
