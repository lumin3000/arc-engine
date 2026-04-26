
const jtask = globalThis.jtask;

// Engine startup flow. Games plug in via two extension points:
//   - StartupFlow.registerTitleScreen(obj)   — pre-startup gate
//   - StartupFlow.registerStartupSteps(arr)  — generator steps run
//                                              between DATA_LOADED and
//                                              MAP_READY (loaded asynchronously
//                                              under LongEventHandler)
//
// The TitleScreen object contract:
//   active   : boolean — true while the title is visible
//   draw()   : called every frame as long as it exists
//
// A "startup step" is a generator function (function*) that may yield
// repeatedly to spread heavy work across frames; each yield value is
// forwarded to LongEventHandler for progress display.
//
// Games register both during bundle evaluation (before the first
// scaffold tick).

globalThis.StartupFlow = {
  _titleScreen: null,
  _steps: [],

  registerTitleScreen: function(ts) {
    if (ts && typeof ts.draw !== 'function') {
      throw new Error("[StartupFlow] TitleScreen must expose draw()");
    }
    this._titleScreen = ts;
  },

  registerStartupSteps: function(steps) {
    if (!Array.isArray(steps)) {
      throw new Error("[StartupFlow] registerStartupSteps expects an array of generator functions");
    }
    for (const s of steps) {
      if (typeof s !== 'function') {
        throw new Error("[StartupFlow] each startup step must be a generator function");
      }
      this._steps.push(s);
    }
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
  }

  for (const step of globalThis.StartupFlow._steps) {
    yield* step();
  }

  if (typeof GameInit !== 'undefined') {
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
