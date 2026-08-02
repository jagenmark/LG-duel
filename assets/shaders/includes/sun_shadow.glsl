// The including shader declares sceneLights and sunShadowMap.
float sunShadowVisibility(vec3 position, vec3 normal) {
  float mapSize = sceneLights.shadowParameters.z;
  if (sceneLights.postParameters.w >= 0.5 || mapSize < 1.0) {
    return 1.0;
  }
  vec3 receiver = position + normalize(normal) * sceneLights.shadowOrigin.w;
  vec3 offset = receiver - sceneLights.shadowOrigin.xyz;
  float extent = max(sceneLights.shadowParameters.x, 0.001);
  vec2 uv = vec2(
    dot(offset, sceneLights.shadowRight.xyz) / extent,
    -dot(offset, sceneLights.shadowUp.xyz) / extent
  ) * 0.5 + 0.5;
  float receiverDepth =
    dot(offset, sceneLights.shadowForward.xyz) /
      max(sceneLights.shadowParameters.y, 0.001) -
    sceneLights.shadowParameters.w;
  if (
    receiverDepth <= 0.0 || receiverDepth >= 1.0 ||
    any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))
  ) {
    return 1.0;
  }
  if (mapSize < 2048.0) {
    return texture(sunShadowMap, vec3(uv, receiverDepth));
  }
  vec2 texel = vec2(1.0 / mapSize);
  float visibility = 0.0;
  visibility += texture(
    sunShadowMap,
    vec3(uv + vec2(-0.5, -0.5) * texel, receiverDepth)
  );
  visibility += texture(
    sunShadowMap,
    vec3(uv + vec2(0.5, -0.5) * texel, receiverDepth)
  );
  visibility += texture(
    sunShadowMap,
    vec3(uv + vec2(-0.5, 0.5) * texel, receiverDepth)
  );
  visibility += texture(
    sunShadowMap,
    vec3(uv + vec2(0.5, 0.5) * texel, receiverDepth)
  );
  return visibility * 0.25;
}
