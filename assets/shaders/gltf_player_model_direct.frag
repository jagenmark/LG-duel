#version 450

layout(location = 0) in vec4 baseColor;
layout(location = 1) in vec4 teamTint;
layout(location = 2) in float tintWeight;
layout(location = 4) in vec3 worldNormal;
layout(location = 7) in vec3 viewDirection;
layout(location = 10) in vec2 ambientData;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0, std140) uniform DirectLightData {
  vec4 sunDirectionIntensity;
  vec4 sunColor;
  vec4 fillColorIntensity;
} directLights;

#include "includes/direct_display.glsl"
#include "includes/live_fill.glsl"
#include "includes/team_tint.glsl"

void main() {
  vec3 base = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));
  vec3 tint = pow(max(teamTint.rgb, vec3(0.0)), vec3(2.2));
  vec3 albedo = applyTeamTint(base, tint, tintWeight);
  if (ambientData.y > 0.5) {
    outColor = vec4(directDisplay(vec3(ambientData.x)), 1.0);
    return;
  }
  vec3 n = normalize(worldNormal);
  vec3 v = normalize(viewDirection);
  vec3 sunDirection =
    normalize(-directLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  vec3 sunRadiance = directLights.sunColor.rgb *
    max(directLights.sunDirectionIntensity.w, 0.0);
  vec3 fillRadiance = directLights.fillColorIntensity.rgb *
    max(directLights.fillColorIntensity.w, 0.0);
  float skyFill = n.z * 0.5 + 0.5;
  vec3 color = albedo * ambientData.x * (
    correctedLiveFill(fillRadiance)
  );
  color += albedo * sunRadiance * sunNDotL;
  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.2);
  rim *= (0.16 + 0.84 * skyFill) * 0.62;
  vec3 rimColor = mix(albedo, vec3(0.58, 0.72, 0.92), 0.36);
  color += rimColor * rim * 0.24;
  outColor = vec4(directDisplay(color), 1.0);
}
