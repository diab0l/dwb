/**
 * Handles extensions. If scripts should be managed by <b>dwbem</b> they must
 * be implemented as an extension. In contrast to regular scripts extensions can
 * be enabled/disabled on the fly without reloading all userscripts.  Every extension must contain two special tags, 
 * <b>/*&lt;INFO ... INFO&gt;*<span><span>/</b> that will be used by dwbem to find information
 * about the extension and <b>//&lt;DEFAULT_CONFIG ... //&gt;DEFAULT_CONFIG</b> that
 * will be used by dwbem to find the default configuration
 * Every extension must also return an object that can have up to four properties.
 *
 *
 * @namespace 
 *      Static object that handles extensions
 * @name extensions 
 * @static 
 *

 * @property {Object} [defaultConfig]
 *      The default configuration, will be passed to extensions.getConfig
 * @property {extensions~onEnd} end 
 *      A function that will be called when the extension is unloaded, can be
 *      used to disconnect from signals/unbind shortcuts, ...
 * @property {Object} [exports]
 *      An object that exports some functionality or the configuration, the
 *      object can be retrieved in other scripts with {@link require}
 * @property {extensions~onInit} init 
 *      A function that will be called when an extension is loaded
 *
 * @example
 * // ext:set ft=javascript:
 *
 * /*&lt;INFO 
 * Extension that does some awesome things
 * INFO&gt;*<span></span>/
 *
 * var defaultConfig = { 
 * //&lt;DEFAULT_CONFIG
 *      // Foo
 *      foo : 37, 
 *      // Shortcut to do some action
 *      shortcut : "foo"
 * //&gt;DEFAULT_CONFIG
 * };
 *
 * var myConfig = {};
 *
 * function action() {
 *      ...
 * } 
 * function onNavigation() {
 *      ...
 * } 
 * var myExtension = {
 *      defaultConfig : defaultConfig, 
 *      exports : {
 *          action : action
 *      },
 *      init : function(config) {
 *          myConfig = config; 
 *          bind(config.shortcut, "action"); 
 *          signals.connect("navigation", onNavigation); 
 *          return true;
 *      },
 *      end : function() {
 *          signals.disconnect(onNavigation);
 *          unbind(action);
 *          return true;
 *      }
 * };
 * return myExtension;
 *
 * */
/**
 * Called when an extension is unloaded
 * @callback extensions~onEnd
 *
 * @returns {Boolean}
 *      Return true if the extension was successfully unloaded
 * */
/**
 * Called when an extension is loaded
 * @callback extensions~onInit
 *
 * @param {Object} configuration 
 *      The configuration passed to {@link extensions.load}
 * @returns {Boolean}
 *      Return true if the extension was successfully initialized
 * */

(function () {
  var _config = {};
  var _registered = {};
  var _configLoaded = false;

  var getPlugin = function(name, filename) 
  {
      var ret = null;
      try 
      {
          if (system.fileTest(filename, FileTest.exists)) 
              ret = include(filename);
      }
      catch(e) 
      {
          extensions.error(name, "Error in line " + e.line + " parsing " + filename);
      }
      return ret;
  };
  var getStack = function(offset) 
  {
      if (arguments.length === 0) 
          offset = 0;

      try 
      {
          throw Error (message);
      }
      catch (e) 
      {
          var stack = e.stack.match(/[^\n]+/g);
          return "STACK: [" + stack.slice(offset+1).join("] [")+"]";
      }
  };
  var _unload = function (name, removeConfig) 
  {
      if (_registered[name] !== undefined) 
      {
          if (_registered[name].end instanceof Function) 
          {
              _registered[name].end();
              extensions.message(name, "Extension unloaded.");
          }
          if (removeConfig)
              delete _config[name];

          delete _registered[name];

          replace(name);

          return true;
      }
      return false;
  };
  Object.defineProperties(extensions, 
  { 
      /**
       * Print a warning message to stderr
       *
       * @memberOf extensions
       * @function
       *
       * @param {String} name 
       *        Name of the extension
       * @param {String} message 
       *        The message to print 
       * */
      "warning" : 
      {
          value : function (name, message) 
          {
              io.print("\033[1mDWB EXTENSION WARNING: \033[0mextension  \033[1m" + name + "\033[0m: " + message, "stderr");
          }
      }, 
      /**
       * Print an error message to stderr
       *
       * @memberOf extensions
       * @function
       *
       * @param {String} name 
       *        Name of the extension
       * @param {String} message 
       *        The message to print 
       * */
      "error" : 
      {
          value : function (name, a, b) {
              var message = "";
              if (a instanceof Error) {
                  if (a.message) {
                      message = a.message;
                  }
                  else if (arguments.length > 2)
                  message = b;
                  else 
                      b = "";
                  io.print("\033[31mDWB EXTENSION ERROR: \033[0mextension \033[1m" + name + "\033[0m in line " + a.line + ": " + 
                          message + "\nSTACK: [" + a.stack.match(/[^\n]+/g).join("] [") + "]", "stderr");
              }
              else {
                  io.print("\033[31mDWB EXTENSION ERROR: \033[0mextension \033[1m" + name + "\033[0m: " + a + "\n" + getStack(1), "stderr");
              }
          }
      },
      /**
       * Print message to stderr
       *
       * @memberOf extensions
       * @function
       *
       * @param {String} name 
       *        Name of the extension
       * @param {String} message 
       *        The message to print 
       * */
      "message" : 
      {
          value : function (name, message) 
          {
              io.print("\033[1mDWB EXTENSION: \033[0mextension \033[1m" + name + "\033[0m: " + message, "stderr");
          }
      },
      /**
       * @name getConfig
       * @memberOf extensions
       * @deprecated use {@link util.mixin}
       * @function
       * */
      "getConfig" : 
      {
          value : function(c, dc) 
          {
              return _deprecated("extensions.getConfig", "util.mixin", arguments);
          }
      }, 
      /**
       * Loads an extension, the default path for an extension is 
       * <i>{@link data.userDataDir}/extensions/name_of_extension</i> or 
       * <i>{@link data.systemDataDir}/extensions/name_of_extension</i>
       *
       * @memberOf extensions
       * @function
       *
       * @param {String} name 
       *        The name of the extension
       * @param {Object} configuration 
       *        The configuration that will be used for the extension
       * */
      "load" : 
      {
          value : function(name, c) 
          {
              if (_registered[name] !== undefined) 
                  extensions.error(name, "Already loaded.");

              var boldname = "\033[1m" + name + "\033[0m";

              var config, dataBase, pluginPath, plugin = null, key, filename;
              var extConfig = null;

              /* Get default config if the config hasn't been read yet */
              if (arguments.length == 2) 
              {
                  extConfig = c;
                  _config[name] = c;
              }
              if (!_configLoaded) 
              {
                  if (system.fileTest(data.configDir + "/extensionrc", FileTest.regular)) 
                  {
                      try 
                      {
                          config = include(data.configDir + "/extensionrc");
                      }
                      catch (e) 
                      {
                          extensions.error(name, "loading config failed : " + e);
                      }
                      if (config === null) 
                      {
                          extensions.warning(name, "Could not load config.");
                      }
                      else 
                      {
                          for (key in config) 
                              _config[key] = config[key];
                      }
                      _configLoaded = true;
                  }
              }
              if (extConfig === null) 
                  extConfig = _config[name] || null;

              /* Load extension */
              if (data.userDataDir) 
              {
                  filename = data.userDataDir + "/extensions/" + name;
                  plugin = getPlugin(name, data.userDataDir + "/extensions/" + name);
              }
              if (plugin === null) 
              {
                  plugin = getPlugin(name, data.systemDataDir + "/extensions/" + name);
                  if (plugin === null) 
                  {
                      extensions.error(name, "Couldn't find extension.");
                      return null;
                  }
              }
              try 
              {
                  plugin._name = name;

                  if (plugin.defaultConfig) 
                      util.mixin(extConfig, plugin.defaultConfig);

                  if (plugin.init(extConfig)) 
                  {
                      _registered[name] = plugin;

                      if (plugin.exports) 
                          replace(name, plugin.exports);

                      extensions.message(name, "Successfully loaded and initialized.");
                      return plugin.exports || null;
                  }
                  else 
                  {
                      extensions.error(name, "Initialization failed.");
                      return null;
                  }
              }
              catch (e) 
              {
                  extensions.error(name, "Initialization failed: " + e);
                  return null;
              }
          }
      },
      /**
       * Unloads an extension, calls extension.end when the extensions is
       * unloaded
       *
       * @memberOf extensions
       * @function
       *
       * @param {String} name 
       *        The name of the extension
       *
       * @returns {Boolean}
       *        true if the extension was found and unloaded
       * */
      "unload" : 
      { 
          value : function(name) 
          {
              return _unload(name, true);
          }
      }, 
      /**
       * Disables all extensions, calls {@link extensions.unload} for every
       * extension
       *
       * @memberOf extensions
       * @function
       *
       * */
      "disableAll" : 
      {
          value : function()
          {
              for (var key in _registered) 
                  _unload(key, true);
          }
      }, 
      /**
       * Toggles an extension, if it is loaded toggle will unload it, otherwise
       * it will load it.
       *
       * @memberOf extensions
       * @function
       * 
       * @param {String} name 
       *        Name of the extension
       * @param {Object} configuration 
       *        Configuration that will be passed to {@link extensions.load}
       *
       * @returns {Boolean}
       *        true if the extension was loaded, false if it was unloaded
       * */
      "toggle" : 
      {
          value : function(name, c) 
          {
              if (_registered[name] !== undefined) 
              {
                  _unload(name);
                  return false;
              }
              else 
              {
                  extensions.load(name, c);
                  return true;
              }
          }
      },
      /**
       * Binds an extension to a shortcut 
       *
       * @memberOf extensions
       * @function
       * 
       * @param {String} name 
       *        Name of the extension
       * @param {String} shortcut 
       *        The shortcut that will be used to toggle the extension
       * @param {Object} options 
       * @param {Boolean} options.load
       *        Whether to initially load the extension 
       * @param {Boolean} options.config
       *        The configuration passed to {@link extensions.load}
       * @param {String} options.command
       *        Command that can be used from dwb's command line to toggle the
       *        extension
       * */
      "bind" : 
      {
          value : function(name, shortcut, options) 
          {
              if (!name || !shortcut)
                  return;

              if (options.load === undefined || options.load) 
                  extensions.load(name, options.config);

              bind(shortcut, function () { 
                  if (extensions.toggle(name, options.config)) 
                      io.notify("Extension " + name + " enabled");
                  else 
                      io.notify("Extension " + name + " disabled");
              }, options.command);
          }
      }
  });
})();
Object.freeze(extensions);
