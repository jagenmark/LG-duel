#version 450

layout(location = 0) in vec3 worldNormal;
layout(location = 2) in vec4 baseColor;
layout(location = 3) in vec2 material;
layout(location = 6) in vec2 ambientData;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0, std140) uniform DirectLightData {
  vec4 sunDirectionIntensity;
  vec4 sunColor;
  vec4 fillColorIntensity;
} directLights;

#include "includes/direct_display.glsl"
#include "includes/live_fill.glsl"

void main() {
  vec3 n = normalize(worldNormal);
  vec3 albedo = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));
  float metallic = clamp(material.x, 0.0, 1.0);
  if (ambientData.y > 0.5) {
    outColor = vec4(directDisplay(vec3(ambientData.x)), 1.0);
    return;
  }
  vec3 fillRadiance = directLights.fillColorIntensity.rgb *
    max(directLights.fillColorIntensity.w, 0.0);
  vec3 color = albedo * ambientData.x * correctedLiveFill(fillRadiance);
  vec3 sunDirection =
    normalize(-directLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  vec3 sunRadiance = directLights.sunColor.rgb *
    max(directLights.sunDirectionIntensity.w, 0.0);
  color += albedo * sunRadiance * sunNDotL *
    (1.0 - metallic * 0.72);
  outColor = vec4(directDisplay(color), 1.0);
}
