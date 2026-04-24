
const jtask = globalThis.jtask;

function* StartupRoutine() {

  if (typeof Root !== 'undefined' && typeof Current !== 'undefined') {
    Root.init();
    Current.root = Root;
    Root.initWindowStack();
  }
  if (typeof Game !== 'undefined' && typeof Current !== 'undefined') {
    Game.init();
    Current.game = Game;
    Game.initManagers();
  }

  while (!message.get_service("loader")) {
    yield;
  }
  jtask.log("[game] Loader service found.");

  if (typeof font !== 'undefined' && typeof font.load === 'function') {
    font.load("res/fonts/LiberationSans.ttf", 1);
    font.load("res/fonts/SourceHanSansSC-Regular.ttf", 2);
  }

  yield;

  if (typeof GameInit !== 'undefined') {
    GameInit.triggerPhase(GameInit.Phase.CLASSES_DEFINED);
    GameInit.triggerPhase(GameInit.Phase.DATA_LOADED);
    GameInit.triggerPhase(GameInit.Phase.MAP_READY);
    GameInit.triggerPhase(GameInit.Phase.GAME_STARTED);
  }

  jtask.log("[game] Startup sequence complete.");
}

function beginStartup() {
  if (typeof LongEventHandler !== 'undefined') {
    LongEventHandler.QueueLongEvent(StartupRoutine, "Initializing...", true);
  } else {
    jtask.log.error("[game] LongEventHandler missing!");
  }
}

// TitleScreen is optional and defined by the game layer (if at all). At
// this point, game scripts haven't been bundled in yet (engine scripts
// load first). Defer the decision to the first scaffold tick — by then
// all game scripts have executed.
(function registerStartupHook() {
  var _decided = false;
  var _systemReady = false;
  ScaffoldCallbacks.register(function() {
    if (!_decided) {
      _decided = true;
      if (typeof globalThis.TitleScreen === 'undefined' || !globalThis.TitleScreen.active) {
        beginStartup();
        return;
      }
    }
    if (typeof globalThis.TitleScreen === 'undefined') return;
    if (globalThis.TitleScreen.active) {
      globalThis.TitleScreen.draw();
      if (globalThis.TitleScreen._startRequested && !_systemReady) {
        _systemReady = true;
        beginStartup();
      }
    }
  }, ScaffoldPriority.LONG_EVENT_CHECK - 1, "TitleScreen.Wait");
})();
