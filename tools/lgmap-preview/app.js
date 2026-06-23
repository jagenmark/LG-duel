const sampleMap = `# LG Duel map format v1.
# Coordinates are project units. Boxes are axis-aligned solids.
version 1
bounds min=-15,-11,0 max=15,11,10

# Thunderstruck's lower central court is ringed by raised fighting lanes.
box lane_north -15,6.5,0 15,11,2
box lane_west -15,-7,0 -10,6.5,2
box lane_east 10,-7,0 15,6.5,2
box spawn_deck_west -15,-11,0 -3,-7,2
box spawn_deck_east 3,-11,0 15,-7,2

# A raised cross-lane overlooks the court while leaving an underpass.
box bridge -10,3,2 10,4.5,2.4

# Opposing five-step stairways connect the lower court to the side lanes.
box stair_west_1 -6.8,-5,0 -6,-2,0.4
box stair_west_2 -7.6,-5,0 -6.8,-2,0.8
box stair_west_3 -8.4,-5,0 -7.6,-2,1.2
box stair_west_4 -9.2,-5,0 -8.4,-2,1.6
box stair_west_5 -10,-5,0 -9.2,-2,2
box stair_east_1 6,-5,0 6.8,-2,0.4
box stair_east_2 6.8,-5,0 7.6,-2,0.8
box stair_east_3 7.6,-5,0 8.4,-2,1.2
box stair_east_4 8.4,-5,0 9.2,-2,1.6
box stair_east_5 9.2,-5,0 10,-2,2

# Low central cover preserves Thunderstruck's exposed tracking lanes.
box cover_left -5,-0.9,0 -3,0.9,1.2
box cover_right 3,-0.9,0 5,0.9,1.2
box center_pillar -1.2,-1.2,0 1.2,1.2,2.6

spawn player_1 -8,-9,2 yaw=0
spawn player_2 8,-9,2 yaw=180
spawn player_3 -12,8,2 yaw=0
spawn player_4 12,8,2 yaw=180
spawn player_5 -3,-9,0 yaw=0
spawn player_6 3,-9,0 yaw=180
`;

const editor = document.querySelector("#mapText");
const canvas = document.querySelector("#preview");
const ctx = canvas.getContext("2d");
const fileInput = document.querySelector("#fileInput");
const statusEl = document.querySelector("#status");
const errorsEl = document.querySelector("#errors");
const statsEl = document.querySelector("#stats");
const exportText = document.querySelector("#exportText");
const loadSampleButton = document.querySelector("#loadSample");
const copyButton = document.querySelector("#copyExport");
const downloadButton = document.querySelector("#downloadExport");

let currentMap = LgMapParser.parseMap(sampleMap);

function worldMetrics(map) {
  const bounds = map.bounds;
  const width = Math.max(1, bounds.max.x - bounds.min.x);
  const height = Math.max(1, bounds.max.y - bounds.min.y);
  const padding = 38;
  const scale = Math.min(
    (canvas.width - padding * 2) / width,
    (canvas.height - padding * 2) / height
  );
  const usedWidth = width * scale;
  const usedHeight = height * scale;
  const originX = (canvas.width - usedWidth) * 0.5;
  const originY = (canvas.height + usedHeight) * 0.5;
  return { bounds, scale, originX, originY };
}

function toScreen(metrics, point) {
  return {
    x: metrics.originX + (point.x - metrics.bounds.min.x) * metrics.scale,
    y: metrics.originY - (point.y - metrics.bounds.min.y) * metrics.scale,
  };
}

function drawGrid(metrics) {
  const minX = Math.ceil(metrics.bounds.min.x);
  const maxX = Math.floor(metrics.bounds.max.x);
  const minY = Math.ceil(metrics.bounds.min.y);
  const maxY = Math.floor(metrics.bounds.max.y);
  ctx.lineWidth = 1;
  ctx.strokeStyle = "#263143";
  for (let x = minX; x <= maxX; x += 1) {
    const a = toScreen(metrics, { x, y: metrics.bounds.min.y });
    const b = toScreen(metrics, { x, y: metrics.bounds.max.y });
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    ctx.stroke();
  }
  for (let y = minY; y <= maxY; y += 1) {
    const a = toScreen(metrics, { x: metrics.bounds.min.x, y });
    const b = toScreen(metrics, { x: metrics.bounds.max.x, y });
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    ctx.stroke();
  }

  ctx.strokeStyle = "#41516b";
  [[0, metrics.bounds.min.y, 0, metrics.bounds.max.y], [metrics.bounds.min.x, 0, metrics.bounds.max.x, 0]]
    .forEach(([x1, y1, x2, y2]) => {
      const a = toScreen(metrics, { x: x1, y: y1 });
      const b = toScreen(metrics, { x: x2, y: y2 });
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    });
}

function drawMap(map) {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "#101620";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  const metrics = worldMetrics(map);
  drawGrid(metrics);

  const min = toScreen(metrics, map.bounds.min);
  const max = toScreen(metrics, map.bounds.max);
  ctx.strokeStyle = map.ok ? "#8bd17c" : "#f05b5b";
  ctx.lineWidth = 3;
  ctx.strokeRect(min.x, max.y, max.x - min.x, min.y - max.y);

  map.boxes.forEach((box) => {
    const a = toScreen(metrics, box.min);
    const b = toScreen(metrics, box.max);
    const heightRatio = Math.min(1, Math.max(0, (box.max.z - map.bounds.min.z) / Math.max(1, map.bounds.max.z - map.bounds.min.z)));
    ctx.fillStyle = `rgba(${90 + heightRatio * 80}, ${132 + heightRatio * 80}, 210, 0.74)`;
    ctx.strokeStyle = "#c1d2ff";
    ctx.lineWidth = 1.5;
    ctx.fillRect(a.x, b.y, b.x - a.x, a.y - b.y);
    ctx.strokeRect(a.x, b.y, b.x - a.x, a.y - b.y);
  });

  map.spawns.forEach((spawn, index) => {
    const point = toScreen(metrics, spawn.position);
    const radius = 7;
    ctx.fillStyle = index < 2 ? "#ffcc66" : "#a7e7ff";
    ctx.strokeStyle = "#101620";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(point.x, point.y, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();

    if (spawn.yaw !== null) {
      const radians = spawn.yaw * Math.PI / 180;
      ctx.strokeStyle = "#ffffff";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(point.x, point.y);
      ctx.lineTo(point.x + Math.cos(radians) * 18, point.y - Math.sin(radians) * 18);
      ctx.stroke();
    }

    ctx.fillStyle = "#ffffff";
    ctx.font = "12px system-ui, sans-serif";
    ctx.fillText(String(index + 1), point.x + 9, point.y - 9);
  });
}

function renderDetails(map) {
  statusEl.textContent = map.ok ? "Valid .lgmap" : `${map.errors.length} issue(s)`;
  statusEl.className = map.ok ? "status ok" : "status bad";
  errorsEl.innerHTML = "";
  if (map.errors.length === 0) {
    const li = document.createElement("li");
    li.textContent = "No parser or validation issues.";
    errorsEl.append(li);
  } else {
    map.errors.forEach((error) => {
      const li = document.createElement("li");
      li.textContent = error;
      errorsEl.append(li);
    });
  }

  const sizeX = map.bounds.max.x - map.bounds.min.x;
  const sizeY = map.bounds.max.y - map.bounds.min.y;
  const sizeZ = map.bounds.max.z - map.bounds.min.z;
  statsEl.innerHTML = "";
  [
    ["Bounds", `${sizeX.toFixed(1)} x ${sizeY.toFixed(1)} x ${sizeZ.toFixed(1)}`],
    ["Boxes", `${map.boxes.length} / ${LgMapParser.MAX_BOXES}`],
    ["Spawns", `${map.spawns.length} / ${LgMapParser.MAX_SPAWNS}`],
  ].forEach(([label, value]) => {
    const row = document.createElement("div");
    row.className = "stat";
    row.innerHTML = `<span>${label}</span><strong>${value}</strong>`;
    statsEl.append(row);
  });

  exportText.value = LgMapParser.serializeMap(map);
}

function update() {
  currentMap = LgMapParser.parseMap(editor.value);
  drawMap(currentMap);
  renderDetails(currentMap);
}

editor.value = sampleMap;
update();

editor.addEventListener("input", update);

loadSampleButton.addEventListener("click", () => {
  editor.value = sampleMap;
  update();
});

fileInput.addEventListener("change", async () => {
  const file = fileInput.files[0];
  if (!file) {
    return;
  }
  editor.value = await file.text();
  update();
});

copyButton.addEventListener("click", async () => {
  exportText.select();
  try {
    await navigator.clipboard.writeText(exportText.value);
    statusEl.textContent = "Export copied";
    statusEl.className = "status ok";
  } catch {
    document.execCommand("copy");
    statusEl.textContent = "Export selected";
  }
});

downloadButton.addEventListener("click", () => {
  const blob = new Blob([exportText.value], { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "preview.lgmap";
  link.click();
  URL.revokeObjectURL(url);
});
