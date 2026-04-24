
function loadJSONFromPtr(ptr_info) {
  if (!ptr_info || !ptr_info.__ptr) {
    return null;
  }
  const size = loader.get_size();
  const str = loader.get_string(ptr_info.__ptr, size);
  return JSON.parse(str);
}

function loadEntitiesFromArray(items, type) {
  if (!items) return;
  for (const item of items) {
    const defId = G.def_register(item.def);
    const x = item.pos?.[0] ?? item.x ?? 0;
    const z = item.pos?.[2] ?? item.z ?? 0;
    const i = G.entity_add({
      type,
      defId,
      id: item.id,
      x,
      z,
      hp: item.health ?? 100,
      growth: Math.floor((item.growth ?? 0) * 10000),
      age: item.age ?? 0,
      unlitTicks: item.unlitTicks ?? 0,
      stuffId: item.stuff ? G.def_register(item.stuff) : 0,
      rot: item.rot ?? 0,
      stack: item.stack ?? item.stackCount ?? 1,
    });
    if (i >= 0) {
      G.grid_add(i, x, z);
    }
  }
}

let loadState = {
  basePath: "",
  files: [],
  fileIndex: 0,
  currentFileLoaded: false
};

G.loader_onFileLoaded = function (msg) {
  if (msg && msg.__ptr) {
    try {
      const data = loadJSONFromPtr(msg);
      const file = loadState.files[loadState.fileIndex];
      if (file && file.handler && data) {
        file.handler(data);
      }
    } catch (e) {
      jtask.log.error("[loader] Parse error: " + e.message);
    }
  }
  loadState.currentFileLoaded = true;
};

function* LoaderRoutine(basePath, onCompleteCallback) {
  jtask.log("[loader] Starting LoaderRoutine: " + basePath);

  while (!message.get_service("loader")) {
    yield;
  }
  const loader_id = message.get_service("loader");

  loadState.basePath = basePath;
  loadState.fileIndex = 0;
  loadState.files = [];

  let summaryLoaded = false;

  loadState.files.push({
    path: basePath + "_summary.json",
    handler: function (data) {
      if (!data) return;
      const mapData = data.maps?.[0];
      const mapInfo = mapData?.mapInfo;
      const w = mapInfo?.size?.[0] || 150;
      const h = mapInfo?.size?.[2] || 150;
      const mapId = mapData?.uniqueID ?? 0;

      G.grid_init(w, h);
      G.section_init(w, h);

      jtask.log(`[loader] Map Inited: ${w}x${h}`);

      const thingsPath = basePath + "_things";
      const prefix = "/map" + mapId + "_";

      const addFile = (name, handler) => {
        loadState.files.push({ path: thingsPath + prefix + name, handler });
      };

      addFile("pawn.json", (d) => { if (d) for (const p of d) G.pawn_add(p); });
      addFile("plant.json", (d) => loadEntitiesFromArray(d, G.EntityType.PLANT));
      addFile("building.json", (d) => loadEntitiesFromArray(d, G.EntityType.BUILDING));

      const buildingFiles = [
        "building_door.json", "building_cooler.json", "building_steamgeyser.json",
        "building_ancientmechremains.json", "building_bed.json", "building_storage.json",
        "building_gravengine.json"
      ];

      for (const f of buildingFiles) {
        addFile(f, (d) => loadEntitiesFromArray(d, G.EntityType.BUILDING));
      }

      addFile("filth.json", (d) => loadEntitiesFromArray(d, G.EntityType.FILTH));
      addFile("thingwithcomps.json", (d) => loadEntitiesFromArray(d, G.EntityType.ITEM));
      addFile("apparel.json", (d) => loadEntitiesFromArray(d, G.EntityType.ITEM));
      addFile("medicine.json", (d) => loadEntitiesFromArray(d, G.EntityType.ITEM));

      summaryLoaded = true;
    }
  });

  while (loadState.fileIndex < loadState.files.length) {
    const file = loadState.files[loadState.fileIndex];

    const traceScaffold = (globalThis.RealTime?.frameCount ?? -1);
    jtask.log("[loader] Requesting: " + file.path + " at frame " + traceScaffold);
    jtask.send(loader_id, "load_for_game", { path: file.path });

    const startTime = RealTime.realtimeSinceStartupUs();

    loadState.currentFileLoaded = false;

    while (!loadState.currentFileLoaded) {
      if (RealTime.realtimeSinceStartupUs() - startTime > 10000000) {
        jtask.log.error("[loader] TIMEOUT waiting for " + file.path);
        break;
      }
      yield;
    }

    loadState.fileIndex++;
  }

  jtask.log("[loader] All files loaded.");
  if (onCompleteCallback) onCompleteCallback();
}

G.loader_runRoutine = LoaderRoutine;

G.loader_startLoad = function (basePath, onComplete) {
  if (typeof LongEventHandler !== 'undefined') {
    const routine = function* () {
      yield* LoaderRoutine(basePath, onComplete);
    };
    LongEventHandler.QueueLongEvent(routine, "LoadingMap", true);
  } else {
    jtask.log.error("[loader] LongEventHandler missing!");
  }
};
