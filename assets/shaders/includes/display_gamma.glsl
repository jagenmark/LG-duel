// Display gamma is intentionally applied after tone mapping and grading.
// White stays white, unlike exposure which changes scene-referred highlights.
vec3 displayEncode(vec3 displayColor, float displayGamma) {
  vec3 encoded = pow(clamp(displayColor, 0.0, 1.0), vec3(1.0 / 2.2));
  float gamma = clamp(displayGamma, 0.50, 1.50);
  return pow(encoded, vec3(1.0 / gamma));
}
