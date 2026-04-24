
const jtask = globalThis.jtask;

const S = {
  _gc: {
    finalize() {
      jtask.log("start_service exit");
    },
  },
};

let game_id = null;

S.external = function (msg) {

  if (game_id === null) {
    game_id = jtask.queryservice("game");
    if (globalThis.__BP_VERBOSE__) {
      jtask.log("[start_service] game_service id=" + game_id);
    }
  }

  if (!game_id) {
    jtask.log.error("[start_service] game_id is null, cannot forward");
    return;
  }

  let parsed = msg;
  if (typeof msg === "string") {
    try {
      parsed = JSON.parse(msg);
    } catch (e) {

      jtask.send(game_id, "frame", msg);
      return;
    }
  }

  if (parsed && parsed.cmd) {
    jtask.send(game_id, "stdin_command", parsed);
  } else {

    jtask.send(game_id, "frame", parsed);
  }
};

const self_id = jtask.self();

jtask.idle_handler(function () {

});

jtask.fork(function () {

  jtask.call(1, "external_forward", self_id, "external");

  message.register_service("start", self_id);

  if (globalThis.__BP_VERBOSE__) {
    jtask.log("[start_service] initialized, self_id=" + self_id);
  }
});

return S;
