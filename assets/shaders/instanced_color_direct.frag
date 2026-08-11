#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 4) in vec2 ambientData;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0, std140) uniform DirectLightData {
  vec4 sunDirectionIntensity;
  vec4 sunColor;
  vec4 fillColorIntensity;
} directLights;

#include "includes/direct_display.glsl"
#include "includes/live_fill.glsl"

void main() {
  vec3 color = vec3(ambientData.x);
  if (ambientData.y <= 0.5) {
    vec3 albedo = pow(max(vertexColor.rgb, vec3(0.0)), vec3(2.2));
    vec3 fillRadiance = directLights.fillColorIntensity.rgb *
      max(directLights.fillColorIntensity.w, 0.0);
    color = albedo * ambientData.x * correctedLiveFill(fillRadiance);
  }
  outColor = vec4(directDisplay(color), 1.0);
}
