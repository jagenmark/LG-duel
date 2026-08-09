// Scene3D encodes map ambient as this RGB scale before it reaches the live
// light uniform. Recover that map-relative energy for dynamic meshes so they
// share the corrected static-world ambient baseline. The 0.90 trim keeps the
// live response conservative while avoiding model-specific fill constants.
const vec3 kSceneFillEncoding = vec3(0.30, 0.36, 0.46);
const float kLiveFillBaselineScale = 0.90;

vec3 correctedLiveFill(vec3 fillRadiance) {
  vec3 mapAmbientRadiance = max(fillRadiance, vec3(0.0)) /
    kSceneFillEncoding;
  return mapAmbientRadiance * kLiveFillBaselineScale;
}
