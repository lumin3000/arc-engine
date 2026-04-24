
const jtask = globalThis.jtask;

// Engine startup flow. Games may plug in a TitleScreen via
// StartupFlow.registerTitleScreen(obj). Without a TitleScreen the
// engine begins the startup sequence immediately; with one, the engine
// draws the title screen each frame and enters startup once the title
// marks itself inactive (e.g. after the user presses Start).
//
// The TitleScreen object contract:
//   active   : boolean — true while the title is visible
//   draw()   : called every frame as long as it exists
//
// Games register the TitleScreen from their own script during bundle
// evaluation (e.g. 98_title_screen.js), which runs before the first
// scaffold tick.

globalThis.StartupFlow = {
  _titleScreen: null,

  registerTitleScreen: function(ts) {
    if (ts && typeof ts.draw !== 'function') {
      throw new Error("[StartupFlow] TitleScreen must expose draw()");
    }
    this._titleScreen = ts;
  },
};

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

var _started = false;
ScaffoldCallbacks.register(function() {
  var ts = globalThis.StartupFlow._titleScreen;
  if (!ts) {
    if (!_started) {
      _started = true;
      beginStartup();
    }
    return;
  }
  if (ts.active) {
    ts.draw();
    return;
  }
  if (!_started) {
    _started = true;
    beginStartup();
  }
}, ScaffoldPriority.LONG_EVENT_CHECK - 1, "TitleScreen.Wait");
