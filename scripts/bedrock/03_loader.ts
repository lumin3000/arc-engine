// Generic file-loader engine. Drives a BlockingTask generator that pulls
// JSON files from the loader service one at a time, feeds them to per-
// file handlers registered by the game.
//
// Flow:
//   1. LoaderRoutine asks loader service to read basePath + summaryName
//   2. The summary handler (registered by the game) inspects the file
//      and calls Loader.addFile(path, handler) for each follow-up file
//      it wants to load. Files added during the summary handler become
//      part of the same BlockingTask — there is no second pass.
//   3. The routine processes the file queue in registration order,
//      invoking each handler with the parsed JSON object.
//   4. When the queue drains, onCompleteCallback fires.
//
// Games register their summary handler at bundle-evaluation time using:
//   Loader.registerSummaryHandler({
//     name: "_summary.json",
//     handler: function(data) { ... Loader.addFile(...); ... }
//   });

function loadJSONFromPtr(ptr_info) {
  if (!ptr_info || !ptr_info.__ptr) {
    return null;
  }
  const size = loader.get_size();
  const str = loader.get_string(ptr_info.__ptr, size);
  return JSON.parse(str);
}

let loadState = {
  basePath: "",
  files: [],
  fileIndex: 0,
  currentFileLoaded: false
};

let _summarySpec = null;

globalThis.Loader = {
  registerSummaryHandler: function(spec) {
    if (!spec || typeof spec.name !== 'string' || typeof spec.handler !== 'function') {
      throw new Error("[Loader] registerSummaryHandler requires { name: string, handler: function }");
    }
    _summarySpec = spec;
  },

  addFile: function(path, handler) {
    if (typeof path !== 'string' || typeof handler !== 'function') {
      throw new Error("[Loader] addFile requires (path: string, handler: function)");
    }
    loadState.files.push({ path: path, handler: handler });
  },

  basePath: function() { return loadState.basePath; },
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

  if (!_summarySpec) {
    jtask.log.error("[loader] No summary handler registered. Call Loader.registerSummaryHandler(...) before starting load.");
    if (onCompleteCallback) onCompleteCallback();
    return;
  }

  while (!message.get_service("loader")) {
    yield;
  }
  const loader_id = message.get_service("loader");

  loadState.basePath = basePath;
  loadState.fileIndex = 0;
  loadState.files = [];

  loadState.files.push({
    path: basePath + _summarySpec.name,
    handler: _summarySpec.handler,
  });

  while (loadState.fileIndex < loadState.files.length) {
    const file = loadState.files[loadState.fileIndex];

    const traceFrame = (globalThis.RealTime?.frameCount ?? -1);
    jtask.log("[loader] Requesting: " + file.path + " at frame " + traceFrame);
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
  if (typeof BlockingTaskQueue !== 'undefined') {
    const routine = function* () {
      yield* LoaderRoutine(basePath, onComplete);
    };
    BlockingTaskQueue.enqueueBlockingTask(routine, "LoadingMap", true);
  } else {
    jtask.log.error("[loader] BlockingTaskQueue missing!");
  }
};
