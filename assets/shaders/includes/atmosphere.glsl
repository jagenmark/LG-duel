const vec3 kAtmosphereHazeColor = vec3(0.0707, 0.0932, 0.1332);

float atmosphereHazeAmount(int quality, float viewDistance) {
  if (quality <= 0) {
    return 0.0;
  }
  float hazeStart = quality == 1 ? 32.0 : quality == 2 ? 28.0 : 24.0;
  float hazeDensity = quality == 1 ? 0.006 : quality == 2 ? 0.010 : 0.014;
  float hazeCap = quality == 1 ? 0.18 : quality == 2 ? 0.32 : 0.42;
  float haze = 1.0 - exp(-max(viewDistance - hazeStart, 0.0) * hazeDensity);
  return min(haze, hazeCap);
}

vec3 applyAtmosphereHaze(vec3 color, int quality, float viewDistance) {
  return mix(color, kAtmosphereHazeColor, atmosphereHazeAmount(quality, viewDistance));
}
