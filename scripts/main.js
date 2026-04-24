
let start;
try {
  start = loadModule(
    "start",
    "external/jtask/jslib/?.js;external/jtask/test/?.js",
  );
  if (typeof start !== "function") {
    throw new Error("start.js did not return a function, got: " + typeof start);
  }
} catch (e) {
  throw e;
}

start({
  core: {
    worker: 8,
    queue: 1024,
    external_queue: 256,
  },
  mainthread: -1,
  service_path: "scripts/rt/?.js;build/?.js;external/jtask/service/?.js",
  bootstrap_path: "external/jtask/jslib/bootstrap.js",
  jslib_path: "external/jtask/jslib/?.js",
  builtin_service_path: "external/jtask/service/?.js",
  bootstrap: [
    {
      name: "timer",
      unique: true,
      worker_id: -1,
    },
    {
      name: "logger",
      unique: true,
      worker_id: -1,
    },
    {
      name: "loader_service",
      unique: true,
      worker_id: -1,
    },
    {
      name: "game_service",
      unique: true,
      worker_id: 1,
    },

    {
      name: "start_service",
      worker_id: -1,
    },
  ],
});
