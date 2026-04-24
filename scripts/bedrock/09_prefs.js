
var Prefs = {};

Prefs.langFolderName = "English";

Prefs._filePath = "prefs.json";

Prefs.load = function () {
  var content = loader.read_string(Prefs._filePath);
  if (!content) {
    jtask.log("[Prefs] No prefs file found, using defaults");
    return;
  }

  try {
    var data = JSON.parse(content);
    if (data.langFolderName) {
      Prefs.langFolderName = data.langFolderName;
    }
    jtask.log("[Prefs] Loaded: langFolderName=" + Prefs.langFolderName);
  } catch (e) {
    jtask.log.error("[Prefs] JSON.parse failed: " + e);
  }
};

Prefs.save = function () {
  var data = {
    langFolderName: Prefs.langFolderName,
  };
  var json = JSON.stringify(data, null, 2);
  loader.write_string(Prefs._filePath, json);
  jtask.log("[Prefs] Saved: langFolderName=" + Prefs.langFolderName);
};

globalThis.Prefs = Prefs;

jtask.log("[bedrock] 09_prefs.js loaded");
