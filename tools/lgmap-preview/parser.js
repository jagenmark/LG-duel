(function(root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  }
  root.LgMapParser = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function() {
  const MAX_BOXES = 24;
  const MAX_SPAWNS = 6;
  const MIN_DUEL_SPAWNS = 2;
  const MAX_COORDINATE_MAGNITUDE = 1000;

  function vec3(x = 0, y = 0, z = 0) {
    return { x, y, z };
  }

  function parseFloatStrict(text) {
    if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(text)) {
      return null;
    }
    const value = Number(text);
    return Number.isFinite(value) ? value : null;
  }

  function parseVec3(text) {
    const parts = text.split(",");
    if (parts.length !== 3) {
      return null;
    }
    const values = parts.map(parseFloatStrict);
    if (values.some((value) => value === null)) {
      return null;
    }
    return vec3(values[0], values[1], values[2]);
  }

  function parseNamedVec3(token, name) {
    const prefix = `${name}=`;
    return token.startsWith(prefix) ? parseVec3(token.slice(prefix.length)) : null;
  }

  function lessThanAll(min, max) {
    return min.x < max.x && min.y < max.y && min.z < max.z;
  }

  function finiteAndReasonable(value) {
    return Number.isFinite(value.x) &&
      Number.isFinite(value.y) &&
      Number.isFinite(value.z) &&
      Math.abs(value.x) <= MAX_COORDINATE_MAGNITUDE &&
      Math.abs(value.y) <= MAX_COORDINATE_MAGNITUDE &&
      Math.abs(value.z) <= MAX_COORDINATE_MAGNITUDE;
  }

  function contains(min, max, point) {
    return point.x >= min.x && point.x <= max.x &&
      point.y >= min.y && point.y <= max.y &&
      point.z >= min.z && point.z <= max.z;
  }

  function containsBox(boundsMin, boundsMax, boxMin, boxMax) {
    return contains(boundsMin, boundsMax, boxMin) &&
      contains(boundsMin, boundsMax, boxMax);
  }

  function boxesOverlap(lhs, rhs) {
    return lhs.min.x < rhs.max.x &&
      lhs.max.x > rhs.min.x &&
      lhs.min.y < rhs.max.y &&
      lhs.max.y > rhs.min.y &&
      lhs.min.z < rhs.max.z &&
      lhs.max.z > rhs.min.z;
  }

  function lineError(lineNumber, message) {
    return `line ${lineNumber}: ${message}`;
  }

  function validateMap(map) {
    const errors = [];
    if (!map.hasVersion) {
      errors.push("map is missing version");
    }
    if (!map.hasBounds) {
      errors.push("map is missing bounds");
    }
    if (!lessThanAll(map.bounds.min, map.bounds.max)) {
      errors.push("bounds min must be lower than bounds max on every axis");
    }
    if (!finiteAndReasonable(map.bounds.min) || !finiteAndReasonable(map.bounds.max)) {
      errors.push("bounds coordinates must be finite and within +/-1000");
    }
    if (map.boxes.length === 0) {
      errors.push("map must define at least one box");
    }
    if (map.spawns.length < MIN_DUEL_SPAWNS) {
      errors.push("map must define at least two spawn points");
    }

    map.boxes.forEach((box, index) => {
      if (!lessThanAll(box.min, box.max)) {
        errors.push(`box ${index} has inverted bounds`);
      }
      if (!finiteAndReasonable(box.min) || !finiteAndReasonable(box.max)) {
        errors.push(`box ${index} coordinates are out of range`);
      }
      if (!containsBox(map.bounds.min, map.bounds.max, box.min, box.max)) {
        errors.push(`box ${index} is outside arena bounds`);
      }
      for (let other = index + 1; other < map.boxes.length; other += 1) {
        if (boxesOverlap(box, map.boxes[other])) {
          errors.push(`box ${index} overlaps box ${other}`);
        }
      }
    });

    map.spawns.forEach((spawn, index) => {
      if (!finiteAndReasonable(spawn.position)) {
        errors.push(`spawn ${index} coordinates are out of range`);
      }
      if (!contains(map.bounds.min, map.bounds.max, spawn.position)) {
        errors.push(`spawn ${index} is outside arena bounds`);
      }
    });

    return errors;
  }

  function parseMap(text) {
    const map = {
      version: 1,
      hasVersion: false,
      hasBounds: false,
      bounds: { min: vec3(-12, -12, 0), max: vec3(12, 12, 8) },
      boxes: [],
      spawns: [],
      parseErrors: [],
    };

    text.split(/\r?\n/).forEach((rawLine, offset) => {
      const lineNumber = offset + 1;
      const line = rawLine.replace(/#.*/, "").trim();
      if (line.length === 0) {
        return;
      }

      const tokens = line.split(/\s+/);
      if (tokens[0] === "version") {
        if (tokens.length !== 2) {
          map.parseErrors.push(lineError(lineNumber, "version expects one integer"));
          return;
        }
        if (!/^[+-]?\d+$/.test(tokens[1])) {
          map.parseErrors.push(lineError(lineNumber, "version must be an integer"));
          return;
        }
        const version = Number(tokens[1]);
        if (version !== 1) {
          map.parseErrors.push(lineError(lineNumber, "only map version 1 is supported"));
          return;
        }
        map.version = version;
        map.hasVersion = true;
      } else if (tokens[0] === "bounds") {
        if (tokens.length !== 3) {
          map.parseErrors.push(lineError(lineNumber, "bounds expects min= and max= vectors"));
          return;
        }
        const min = parseNamedVec3(tokens[1], "min");
        const max = parseNamedVec3(tokens[2], "max");
        if (!min || !max) {
          map.parseErrors.push(lineError(lineNumber, "bounds vectors must be min=x,y,z max=x,y,z"));
          return;
        }
        map.bounds = { min, max };
        map.hasBounds = true;
      } else if (tokens[0] === "box") {
        if (tokens.length !== 4) {
          map.parseErrors.push(lineError(lineNumber, "box expects id min max"));
          return;
        }
        if (map.boxes.length >= MAX_BOXES) {
          map.parseErrors.push(lineError(lineNumber, "too many boxes"));
          return;
        }
        const min = parseVec3(tokens[2]);
        const max = parseVec3(tokens[3]);
        if (!min || !max) {
          map.parseErrors.push(lineError(lineNumber, "box vectors must be x,y,z"));
          return;
        }
        map.boxes.push({ id: tokens[1], min, max, lineNumber });
      } else if (tokens[0] === "spawn") {
        if (tokens.length < 3 || tokens.length > 4) {
          map.parseErrors.push(lineError(lineNumber, "spawn expects id position [yaw=degrees]"));
          return;
        }
        if (map.spawns.length >= MAX_SPAWNS) {
          map.parseErrors.push(lineError(lineNumber, "too many spawn points"));
          return;
        }
        const position = parseVec3(tokens[2]);
        if (!position) {
          map.parseErrors.push(lineError(lineNumber, "spawn position must be x,y,z"));
          return;
        }
        let yaw = null;
        if (tokens.length === 4) {
          if (!tokens[3].startsWith("yaw=")) {
            map.parseErrors.push(lineError(lineNumber, "spawn option must be yaw=degrees"));
            return;
          }
          yaw = parseFloatStrict(tokens[3].slice(4));
          if (yaw === null) {
            map.parseErrors.push(lineError(lineNumber, "spawn yaw must be a finite number"));
            return;
          }
        }
        map.spawns.push({ id: tokens[1], position, yaw, lineNumber });
      } else {
        map.parseErrors.push(lineError(lineNumber, `unknown directive '${tokens[0]}'`));
      }
    });

    map.validationErrors = validateMap(map);
    map.errors = [...map.parseErrors, ...map.validationErrors];
    map.ok = map.errors.length === 0;
    return map;
  }

  function formatNumber(value) {
    if (Object.is(value, -0)) {
      return "0";
    }
    return Number(value.toFixed(4)).toString();
  }

  function formatVec3(value) {
    return `${formatNumber(value.x)},${formatNumber(value.y)},${formatNumber(value.z)}`;
  }

  function serializeMap(map) {
    const lines = [
      "# LG Duel map format v1.",
      "# Exported by tools/lgmap-preview.",
      "version 1",
      `bounds min=${formatVec3(map.bounds.min)} max=${formatVec3(map.bounds.max)}`,
      "",
    ];

    map.boxes.forEach((box) => {
      lines.push(`box ${box.id} ${formatVec3(box.min)} ${formatVec3(box.max)}`);
    });
    lines.push("");
    map.spawns.forEach((spawn) => {
      const yaw = spawn.yaw === null ? "" : ` yaw=${formatNumber(spawn.yaw)}`;
      lines.push(`spawn ${spawn.id} ${formatVec3(spawn.position)}${yaw}`);
    });
    lines.push("");
    return lines.join("\n");
  }

  return {
    MAX_BOXES,
    MAX_SPAWNS,
    parseMap,
    serializeMap,
  };
});
