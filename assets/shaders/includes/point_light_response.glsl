// Shared diffuse response for material-lit point-light paths. Material quality
// must not change this base light contribution.
vec3 pointLightDiffuseResponse(
  vec3 albedo,
  vec3 radiance,
  float nDotL
) {
  return albedo * radiance * max(nDotL, 0.0);
}
