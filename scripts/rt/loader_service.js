
const jtask = globalThis.jtask;
const message = globalThis.message;
const loader = globalThis.loader;
const self_id = jtask.self();

const S = {
  _gc: {
    finalize() {
      jtask.log("[loader] exit");
    },
  },
};

S.load = function (msg) {
  const path = msg.path;
  if (!path) {
    jtask.log.error("[loader] load: missing path");
    return null;
  }

  const result = loader.read(path);
  if (!result) {

    return null;
  }

  if (globalThis.__BP_VERBOSE__) {
    jtask.log("[loader] loaded: " + path + " (" + result.size + " bytes)");
  }
  return result;
};

S.load_for_game = function (msg) {
  const path = msg.path;
  if (!path) {
    jtask.log.error("[loader] load_for_game: missing path");
    return;
  }
  const result = loader.read(path);

  const game_id = message.get_service("game");
  if (game_id) {
    jtask.send(game_id, "file_loaded", result || {});
  } else {
    jtask.log.error("[loader_service] Game service not found!");
  }
};

S.free = function (msg) {
  loader.free();
  return { ok: true };
};

jtask.fork(function () {
  message.register_service("loader", self_id);
  if (globalThis.__BP_VERBOSE__) {
    jtask.log("[loader] ready, id=" + self_id);
  }
});

return S;
