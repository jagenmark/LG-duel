#version 450

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec3 viewDirection;
layout(location = 2) in vec4 baseColor;
layout(location = 3) in vec2 material;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform samplerCube weaponEnvironment;

const float PI = 3.14159265359;

vec3 filmicToneMap(vec3 color) {
  // ACES approximation retains bright metal glints without flattening the
  // mid-tone separation between the revolver's different steel materials.
  return clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
}

void main() {
  vec3 n = normalize(worldNormal);
  vec3 v = normalize(viewDirection);
  vec3 l = normalize(vec3(-0.35, -0.45, 0.82));
  vec3 h = normalize(v + l);
  float metallic = clamp(material.x, 0.0, 1.0);
  float roughness = clamp(material.y, 0.08, 1.0);
  // Exported Blender base colors are stored as sRGB bytes in the baked mesh.
  // Lighting must operate in linear space or the authored material contrast
  // is lost unpredictably when tone mapping is applied.
  vec3 albedo = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));

  float nDotL = max(dot(n, l), 0.0);
  float nDotV = max(dot(n, v), 0.001);
  float nDotH = max(dot(n, h), 0.0);
  float vDotH = max(dot(v, h), 0.0);
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
  float distribution = alpha2 / max(PI * denominator * denominator, 0.001);
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float geometryV = nDotV / (nDotV * (1.0 - k) + k);
  float geometryL = nDotL / (nDotL * (1.0 - k) + k);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
  vec3 specular = distribution * geometryV * geometryL * fresnel /
    max(4.0 * nDotV * nDotL, 0.001);
  vec3 diffuse = (1.0 - metallic) * (1.0 - fresnel) * albedo / PI;

  vec3 reflected = reflect(-v, n);
  // Rough materials sample blurrier cubemap mips, while polished steel keeps
  // the authored light cards sharp enough to move visibly over curved parts.
  vec3 environment = textureLod(weaponEnvironment, reflected, roughness * 6.0).rgb;
  vec3 environmentSpecular = environment * f0 * mix(1.35, 0.55, roughness);
  vec3 ambientDiffuse = albedo * (1.0 - metallic) * 0.30;
  vec3 color = ambientDiffuse + (diffuse + specular) * nDotL * 4.0 +
    environmentSpecular;
  color = filmicToneMap(color * 1.8);
  color = pow(color, vec3(1.0 / 2.2));
  outColor = vec4(color, baseColor.a);
}
