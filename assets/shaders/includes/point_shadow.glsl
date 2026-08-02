// The including shader declares sceneLights and pointShadowMap.
float pointShadowVisibility(int index, vec3 position, vec3 normal) {
  int shadowSlot = int(sceneLights.lightParameters[index].z + 0.5) - 1;
  if (
    sceneLights.postParameters.w >= 0.5 ||
    shadowSlot < 0 ||
    sceneLights.pointShadowParameters.x < 1.0
  ) {
    return 1.0;
  }
  float radius = max(sceneLights.positionRadius[index].w, 0.05);
  float sourceRadius = clamp(sceneLights.lightParameters[index].x, 0.0, radius);
  vec3 receiver = position + normalize(normal) *
    max(0.012, sourceRadius * 0.12);
  vec3 direction = receiver - sceneLights.positionRadius[index].xyz;
  vec3 absoluteDirection = abs(direction);
  float major = max(
    absoluteDirection.x,
    max(absoluteDirection.y, absoluteDirection.z)
  );
  if (major <= 0.001 || major >= radius) {
    return 1.0;
  }
  int face = 0;
  vec2 projected = vec2(0.0);
  if (
    absoluteDirection.x >= absoluteDirection.y &&
    absoluteDirection.x >= absoluteDirection.z
  ) {
    if (direction.x >= 0.0) {
      face = 0;
      projected = vec2(-direction.y, -direction.z);
    } else {
      face = 1;
      projected = vec2(direction.y, -direction.z);
    }
  } else if (absoluteDirection.y >= absoluteDirection.z) {
    if (direction.y >= 0.0) {
      face = 2;
      projected = vec2(direction.x, -direction.z);
    } else {
      face = 3;
      projected = vec2(-direction.x, -direction.z);
    }
  } else if (direction.z >= 0.0) {
    face = 4;
    projected = vec2(direction.x, -direction.y);
  } else {
    face = 5;
    projected = vec2(-direction.x, -direction.y);
  }
  vec2 uv = projected / major * 0.5 + 0.5;
  float nearPlane = clamp(
    max(0.025, sourceRadius * 0.25),
    0.025,
    max(0.025, radius * 0.25)
  );
  float receiverDepth =
    radius / max(radius - nearPlane, 0.001) -
    (nearPlane * radius) / max(radius - nearPlane, 0.001) / major -
    sceneLights.pointShadowParameters.z -
    (sourceRadius / radius) * 0.0015;
  float layer = float(shadowSlot * 6 + face);
  float mapSize = sceneLights.pointShadowParameters.x;
  float softness = clamp(sourceRadius / radius, 0.0, 1.0);
  if (softness <= 0.0001) {
    return textureLod(pointShadowMap, vec4(uv, layer, receiverDepth), 0.0);
  }
  float kernelTexels = mapSize < 512.0
    ? min(0.5 + softness * 0.75, 1.0)
    : min(0.65 + softness * 1.35, 2.0);
  vec2 texel = vec2(kernelTexels / mapSize);
  float visibility = 0.0;
  visibility += textureLod(
    pointShadowMap,
    vec4(uv + vec2(-texel.x, -texel.y), layer, receiverDepth),
    0.0
  );
  visibility += textureLod(
    pointShadowMap,
    vec4(uv + vec2(texel.x, -texel.y), layer, receiverDepth),
    0.0
  );
  visibility += textureLod(
    pointShadowMap,
    vec4(uv + vec2(-texel.x, texel.y), layer, receiverDepth),
    0.0
  );
  visibility += textureLod(
    pointShadowMap,
    vec4(uv + texel, layer, receiverDepth),
    0.0
  );
  return visibility * 0.25;
}
