
var EnginePrefs = {};

EnginePrefs.langFolderName = "English";

EnginePrefs._filePath = "prefs.json";

EnginePrefs.load = function () {
  var content = loader.read_string(EnginePrefs._filePath);
  if (!content) {
    jtask.log("[EnginePrefs] No prefs file found, using defaults");
    return;
  }

  try {
    var data = JSON.parse(content);
    if (data.langFolderName) {
      EnginePrefs.langFolderName = data.langFolderName;
    }
    jtask.log("[EnginePrefs] Loaded: langFolderName=" + EnginePrefs.langFolderName);
  } catch (e) {
    jtask.log.error("[EnginePrefs] JSON.parse failed: " + e);
  }
};

EnginePrefs.save = function () {
  var data = {
    langFolderName: EnginePrefs.langFolderName,
  };
  var json = JSON.stringify(data, null, 2);
  loader.write_string(EnginePrefs._filePath, json);
  jtask.log("[EnginePrefs] Saved: langFolderName=" + EnginePrefs.langFolderName);
};

globalThis.EnginePrefs = EnginePrefs;

jtask.log("[bedrock] 09_prefs.js loaded");
