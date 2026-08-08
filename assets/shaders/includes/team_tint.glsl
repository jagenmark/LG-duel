vec3 applyTeamTint(vec3 albedo, vec3 tint, float weight) {
  float sourceValue = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
  vec3 tinted = tint * (0.34 + 0.66 * sqrt(clamp(sourceValue, 0.0, 1.0)));
  return mix(albedo, tinted, clamp(weight, 0.0, 1.0));
}
