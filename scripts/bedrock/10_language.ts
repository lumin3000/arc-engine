
(function() {
  var currentLang = "English";
  var keys = {};

  function loadLanguage(lang) {
    var path = "Languages/" + lang + "/Keyed/Keys.json";
    var content = loader.read_string(path);
    if (!content) {
      jtask.log("[Lang] Failed to load: " + path);
      return false;
    }
    try {
      keys = JSON.parse(content);
      currentLang = lang;
      jtask.log("[Lang] Loaded: " + lang + " (" + Object.keys(keys).length + " keys)");
      return true;
    } catch (e) {
      jtask.log("[Lang] Parse error: " + path + " " + e.message);
      return false;
    }
  }

  globalThis.G = globalThis.G || {};

  G.Translate = function(key) {
    return keys[key] || key;
  };

  G.GetLanguage = function() {
    return currentLang;
  };

  G.SetLanguage = function(lang) {
    loadLanguage(lang);
  };

  G.ToggleLanguage = function() {
    if (currentLang === "English") {
      loadLanguage("ChineseSimplified");
    } else {
      loadLanguage("English");
    }
  };

  loadLanguage("English");
})();
