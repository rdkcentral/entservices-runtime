(function(global) {

  "use strict";

  // ---------------------------------------------------------------------------
  // Private state
  // ---------------------------------------------------------------------------
  var __transport = null;
  var _connecting = false;
  var _connected = false;
  var _fireboltInstance = null;
  var _connectionResolvers = [];
  var _nextId = 1;
  var _pendingCalls = Object.create(null); // id → { isSubscribe, resolve, reject, [resultSchema] }
  var _eventListeners = Object.create(null); // "Module.onEvent" → [callbacks]

  // ---------------------------------------------------------------------------
  // Schema validator
  // ---------------------------------------------------------------------------
  function _resolveRef(name) {
    return _typeSchemas[name] || null;
  }

  function _validate(value, schema) {
    if (!schema || schema.kind === "null") return null;
    switch (schema.kind) {
      case "primitive":   return _validatePrimitive(value, schema);
      case "ref":         return _validate(value, _resolveRef(schema.name));
      case "object":      return _validateObject(value, schema);
      case "array":       return _validateArray(value, schema);
      case "optional":    return (value === null || value === undefined) ? null : _validate(value, schema.inner);
      case "union":       return _validateUnion(value, schema);
      case "enum":        return _validateEnum(value, schema);
      default:            return null;
    }
  }

  function _validatePrimitive(value, schema) {
    if (schema.type === "bool"   && typeof value !== "boolean") return "expected boolean, got " + typeof value;
    if (schema.type === "string" && typeof value !== "string")  return "expected string, got " + typeof value;
    if (schema.type === "number" && typeof value !== "number")  return "expected number, got " + typeof value;
    if (schema.type === "string" && schema.constraints) {
      var c = schema.constraints;
      if (c.minLength !== undefined && value.length < c.minLength)
        return "minLength violation: " + value.length + " < " + c.minLength;
      if (c.maxLength !== undefined && value.length > c.maxLength)
        return "maxLength violation: " + value.length + " > " + c.maxLength;
      if (c.pattern !== undefined && !(new RegExp(c.pattern)).test(value))
        return "pattern violation: " + c.pattern;
    }
    if ((schema.type === "number") && schema.constraints) {
      var cn = schema.constraints;
      if (cn.minimum !== undefined && value < cn.minimum)
        return "minimum violation: " + value + " < " + cn.minimum;
      if (cn.maximum !== undefined && value > cn.maximum)
        return "maximum violation: " + value + " > " + cn.maximum;
    }
    return null;
  }

  function _validateObject(value, schema) {
    if (typeof value !== "object" || value === null || Array.isArray(value))
      return "expected object";
    var required = schema.required || [];
    for (var i = 0; i < required.length; i++) {
      if (!(required[i] in value)) return required[i] + ": required field missing";
    }
    var props = schema.properties || {};
    for (var key in props) {
      if (key in value) {
        var err = _validate(value[key], props[key]);
        if (err) return key + ": " + err;
      }
    }
    return null;
  }

  function _validateArray(value, schema) {
    if (!Array.isArray(value)) return "expected array";
    for (var i = 0; i < value.length; i++) {
      var err = _validate(value[i], schema.items);
      if (err) return "[" + i + "]: " + err;
    }
    return null;
  }

  function _validateUnion(value, schema) {
    var variants = schema.variants || [];
    for (var i = 0; i < variants.length; i++) {
      if (_validate(value, variants[i]) === null) return null;
    }
    return "no union variant matched";
  }

  function _validateEnum(value, schema) {
    var values = schema.values || [];
    for (var i = 0; i < values.length; i++) {
      if (value === values[i]) return null;
    }
    return "expected one of [" + values.join(", ") + "], got " + JSON.stringify(value);
  }

  // --- Generated data ---
  var _VERSION = "9.0";
  
  var _typeSchemas = {
    "Accessibility.ClosedCaptionsSettings": {"kind":"object","properties":{"enabled":{"kind":"primitive","type":"bool"},"preferredLanguages":{"kind":"optional","inner":{"kind":"array","items":{"kind":"primitive","type":"string"}}}},"required":["enabled"]},
    "Accessibility.VoiceGuidanceSettings": {"kind":"object","properties":{"enabled":{"kind":"primitive","type":"bool"},"rate":{"kind":"primitive","type":"number","constraints":{"minimum":0.1,"maximum":10}},"navigationHints":{"kind":"primitive","type":"bool"}},"required":["enabled","rate","navigationHints"]},
    "Actions.IntentPayload": {"kind":"object","properties":{"intentId":{"kind":"primitive","type":"number"},"intent":{"kind":"primitive","type":"string"}},"required":["intentId","intent"]},
    "Advertising.AdvertisingId": {"kind":"object","properties":{"ifa":{"kind":"primitive","type":"string"},"ifa_type":{"kind":"ref","name":"Advertising.IfaType"},"lmt":{"kind":"ref","name":"Advertising.Lmt"}},"required":["ifa","ifa_type","lmt"]},
    "Advertising.IfaType": {"kind":"enum","values":["dpid","sspid","sessionid"]},
    "Advertising.Lmt": {"kind":"enum","values":["0","1"]},
    "Device.DeviceClass": {"kind":"enum","values":["ott","stb","tv"]},
    "Device.HdrCapabilities": {"kind":"object","properties":{"hdr10":{"kind":"primitive","type":"bool"},"hdr10Plus":{"kind":"primitive","type":"bool"},"dolbyVision":{"kind":"primitive","type":"bool"},"hlg":{"kind":"primitive","type":"bool"}},"required":["hdr10","hdr10Plus","dolbyVision","hlg"]},
    "Discovery.AgePolicy": {"kind":"enum","values":["app:adult","app:child","app:teen"]},
    "Display.ColorimetryValue": {"kind":"enum","values":["SDR","HDR"]},
    "Display.VideoResolution": {"kind":"enum","values":["1920x1080","3840x2160","7680x4320"]},
    "Metrics.ErrorType": {"kind":"enum","values":["network","playback","entitlement","parse","aborted","unknown"]}
  };
  
  var _methodRegistry = {
    "Accessibility.audioDescription": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"bool"}},
    "Accessibility.onAudioDescriptionChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"bool"}},
    "Accessibility.closedCaptionsSettings": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Accessibility.ClosedCaptionsSettings"}},
    "Accessibility.onClosedCaptionsSettingsChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Accessibility.ClosedCaptionsSettings"}},
    "Accessibility.highContrastUI": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"bool"}},
    "Accessibility.onHighContrastUIChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"bool"}},
    "Accessibility.voiceGuidanceSettings": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Accessibility.VoiceGuidanceSettings"}},
    "Accessibility.onVoiceGuidanceSettingsChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Accessibility.VoiceGuidanceSettings"}},
    "Actions.start": {"kind":"call","paramsSchema":{"kind":"object","properties":{"intent":{"kind":"primitive","type":"string"},"handlerAppId":{"kind":"optional","inner":{"kind":"primitive","type":"string"}}},"required":["intent"]},"resultSchema":{"kind":"null"}},
    "Actions.intent": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Actions.IntentPayload"}},
    "Actions.onIntent": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Actions.IntentPayload"}},
    "Advertising.advertisingId": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Advertising.AdvertisingId"}},
    "Device.uid": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"string"}},
    "Device.deviceClass": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Device.DeviceClass"}},
    "Device.hdr": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Device.HdrCapabilities"}},
    "Device.onHdrChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Device.HdrCapabilities"}},
    "Device.dolbyAtmosExperienceAvailable": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"bool"}},
    "Device.onDolbyAtmosExperienceAvailableChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"bool"}},
    "Discovery.watched": {"kind":"call","paramsSchema":{"kind":"object","properties":{"entityId":{"kind":"primitive","type":"string"},"progress":{"kind":"optional","inner":{"kind":"primitive","type":"number"}},"completed":{"kind":"optional","inner":{"kind":"primitive","type":"bool"}},"watchedOn":{"kind":"optional","inner":{"kind":"primitive","type":"string"}},"agePolicy":{"kind":"optional","inner":{"kind":"ref","name":"Discovery.AgePolicy"}}},"required":["entityId"]},"resultSchema":{"kind":"null"}},
    "Display.colorimetry": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Display.ColorimetryValue"}},
    "Display.videoResolutions": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"array","items":{"kind":"ref","name":"Display.VideoResolution"}}},
    "Localization.country": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"string","constraints":{"minLength":2,"maxLength":2,"pattern":"^[A-Z]{2}$"}}},
    "Localization.onCountryChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"string","constraints":{"minLength":2,"maxLength":2,"pattern":"^[A-Z]{2}$"}}},
    "Localization.preferredAudioLanguages": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"array","items":{"kind":"primitive","type":"string"}}},
    "Localization.onPreferredAudioLanguagesChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"array","items":{"kind":"primitive","type":"string"}}},
    "Localization.presentationLanguage": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"string"}},
    "Localization.onPresentationLanguageChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"string"}},
    "Metrics.ready": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.startContent": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.stopContent": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.page": {"kind":"call","paramsSchema":{"kind":"object","properties":{"pageName":{"kind":"primitive","type":"string"}},"required":["pageName"]},"resultSchema":{"kind":"null"}},
    "Metrics.error": {"kind":"call","paramsSchema":{"kind":"object","properties":{"errorType":{"kind":"ref","name":"Metrics.ErrorType"},"errorMessage":{"kind":"optional","inner":{"kind":"primitive","type":"string"}}},"required":["errorType"]},"resultSchema":{"kind":"null"}},
    "Metrics.mediaLoadStart": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaPlay": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaPlaying": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaPause": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaWaiting": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaSeeking": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaSeeked": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaRateChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaRenditionChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.mediaEnded": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"null"}},
    "Metrics.event": {"kind":"call","paramsSchema":{"kind":"object","properties":{"eventName":{"kind":"primitive","type":"string"},"eventData":{"kind":"optional","inner":{"kind":"primitive","type":"string"}}},"required":["eventName"]},"resultSchema":{"kind":"null"}},
    "Metrics.appInfo": {"kind":"call","paramsSchema":{"kind":"object","properties":{"agePolicy":{"kind":"optional","inner":{"kind":"ref","name":"Shared.AgePolicy"}}},"required":[]},"resultSchema":{"kind":"null"}},
    "Network.connected": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"bool"}},
    "Network.onConnectedChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"primitive","type":"bool"}},
    "VideoOutput.resolution": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"VideoOutput.VideoResolution"}},
    "VideoOutput.onResolutionChanged": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"VideoOutput.VideoResolution"}}
  };
  
  // --- End generated data ---

  // ---------------------------------------------------------------------------
  // Transport layer
  // ---------------------------------------------------------------------------
  function _onMessage(raw) {
    var message;

    // if raw is string, parse as JSON-RPC; otherwise ignore (could be non-JSON message from transport)
    if (typeof raw === "string") {
      console.log("Received message: " + raw);
      try { 
        message = JSON.parse(raw); 
      } catch (e) {
        console.log("Failed to parse message: " + raw);
        return;
      }
    } else if (typeof raw === "object") {
      console.log("Received non-string message: ", JSON.stringify(raw));
      message = raw;
    }

    // Has id → call response or subscribe ack
    if (message.id !== undefined) {
      var pending = _pendingCalls[message.id];
      if (!pending) return;
      delete _pendingCalls[message.id];

      if (message.error) {
        var errMsg = (message.error.message || "Firebolt error") + " (code: " + message.error.code + ")";
        if (pending.isSubscribe) {
          // Remove eagerly-registered listener on subscribe failure
          var listeners = _eventListeners[pending.eventName];
          if (listeners) {
            var idx = listeners.indexOf(pending.callback);
            if (idx !== -1) listeners.splice(idx, 1);
          }
        }
        pending.reject(new Error(errMsg));
        return;
      }

      if (pending.isSubscribe) {
        // result: null → subscription confirmed; resolve with unsubscribe fn
        pending.resolve(pending.unsubscribeFn);
        return;
      }

      // Regular call response — validate result
      if (pending.resultSchema) {
        var valErr = _validate(message.result, pending.resultSchema);
        if (valErr) {
          pending.reject(new Error("Invalid result from " + pending.methodName + ": " + valErr));
          return;
        }
      }
      pending.resolve(message.result);
      return;
    }

    // No id, has method → Firebolt 9 event notification
    if (message.method) {
      var eventName = message.method;
      var entry = _methodRegistry[eventName];
      if (!entry || entry.kind !== "subscribe") return;

      var payload = entry.eventIsPrimitive
        ? (message.params ? message.params.value : undefined)
        : message.params;

      if (entry.eventSchema) {
        var evErr = _validate(payload, entry.eventSchema);
        if (evErr) {
          console.warn("Firebolt: invalid event payload for " + eventName + ": " + evErr);
          return;
        }
      }

      var cbs = _eventListeners[eventName];
      if (cbs) {
        for (var i = 0; i < cbs.length; i++) { cbs[i](payload); }
      }
    }
  }

  function _onStatus(status) {
    console.log("Transport status changed: " + status);
    _connected = (status === "connected");
    if (_connected) {
      if (!_fireboltInstance) { _fireboltInstance = _buildFireboltInstance(); }
      var resolvers = _connectionResolvers.splice(0);
      for (var i = 0; i < resolvers.length; i++) { resolvers[i](_fireboltInstance); }
    }
  }

  function _rpcCall(methodName, params) {
    var entry = _methodRegistry[methodName];
    if (entry && entry.paramsSchema) {
      var pErr = _validate(params, entry.paramsSchema);
      if (pErr) return Promise.reject(new Error("Invalid params for " + methodName + ": " + pErr));
    }
    return new Promise(function (resolve, reject) {
      var id = _nextId++;
      var t = __transport;
      _pendingCalls[id] = {
        isSubscribe: false,
        methodName: methodName,
        resultSchema: entry ? entry.resultSchema : null,
        resolve: resolve,
        reject: reject,
      };
      var msg = JSON.stringify({ jsonrpc: "2.0", id: id, method: methodName, params: params || {} });
      console.log("Sending message: " + msg);
      var result = t.send(msg);
      if (!result.success) {
        delete _pendingCalls[id];
        reject(new Error("Transport send failed (errorCode: " + result.errorCode + ")"));
      }
    });
  }

  function _subscribe(eventName, callback) {
    if (!_eventListeners[eventName]) { _eventListeners[eventName] = []; }
    _eventListeners[eventName].push(callback); // eager registration

    return new Promise(function (resolve, reject) {
      var id = _nextId++;
      var t = __transport;

      function unsubscribeFn() {
        var ls = _eventListeners[eventName];
        if (ls) {
          var i = ls.indexOf(callback);
          if (i !== -1) ls.splice(i, 1);
        }
        if (!ls || ls.length === 0) {
          var unsubId = _nextId++;
          _pendingCalls[unsubId] = { isSubscribe: true, eventName: eventName, callback: null, unsubscribeFn: null, resolve: function(){}, reject: function(){} };
          var unsubMsg = JSON.stringify({ jsonrpc: "2.0", id: unsubId, method: eventName, params: { listen: false } });
          t.send(unsubMsg);
        }
      }

      _pendingCalls[id] = {
        isSubscribe: true,
        eventName: eventName,
        callback: callback,
        unsubscribeFn: unsubscribeFn,
        resolve: resolve,
        reject: reject,
      };

      var msg = JSON.stringify({ jsonrpc: "2.0", id: id, method: eventName, params: { listen: true } });
      var result = t.send(msg);
      if (!result.success) {
        console.warn("Transport send failed (errorCode: " + result.errorCode + ")");
        delete _pendingCalls[id];
        var ls = _eventListeners[eventName];
        if (ls) { var i = ls.indexOf(callback); if (i !== -1) ls.splice(i, 1); }
        reject(new Error("Transport send failed (errorCode: " + result.errorCode + ")"));
      }
    });
  }

  // ---------------------------------------------------------------------------
  // Stub factories
  // ---------------------------------------------------------------------------
  function _makeCallStub(fullMethodName) {
    return function (params) {
      return _rpcCall(fullMethodName, params || {});
    };
  }

  function _makeSubscribeStub(fullMethodName) {
    return function (callback) {
      return _subscribe(fullMethodName, callback);
    };
  }

  // ---------------------------------------------------------------------------
  // FireboltClient builder
  // ---------------------------------------------------------------------------
  function _buildFireboltInstance() {
    var modules = Object.create(null);
    for (var fullName in _methodRegistry) {
      var dotIdx = fullName.indexOf(".");
      var modName = fullName.slice(0, dotIdx);
      var methodName = fullName.slice(dotIdx + 1);
      var desc = _methodRegistry[fullName];
      if (!modules[modName]) { modules[modName] = Object.create(null); }
      modules[modName][methodName] = desc.kind === "subscribe"
        ? _makeSubscribeStub(fullName)
        : _makeCallStub(fullName);
    }
    var client = Object.create(null);
    for (var mod in modules) { client[mod] = Object.freeze(modules[mod]); }
    return Object.freeze(client);
  }

  // ---------------------------------------------------------------------------
  // configure / get
  // ---------------------------------------------------------------------------
  function _transport(transport) {
    console.log("Configuring FireboltServiceManager with transport: ");
    if (typeof transport === "object") {
      console.log("transport is an object");
      // check if transport has required methods individually to provide better error messages
      if (typeof transport.connect !== "function") {
        console.warn("Transport object is missing required method: connect");
        return;
      }
      if (typeof transport.send !== "function") {
        console.warn("Transport object is missing required method: send");
        return;
      }
      if (typeof transport.onMessage !== "function") {
        console.warn("Transport object is missing required method: onMessage");
        return;
      }
      if (typeof transport.onConnectionStatus !== "function") {
        console.warn("Transport object is missing required method: onConnectionStatus");
        return;
      }

    } else {
      console.warn("Invalid transport passed to FireboltServiceManager.configure: ", transport);
    }
    __transport = transport;
  }

  function _get() {
    if (!__transport) {
      throw new Error(
        "FireboltServiceManager.get() called before configure(). " +
        "The WPE extension must call configure({ clientId }) first."
      );
    }
    console.log("FireboltServiceManager.get() called");
    if (_connected && _fireboltInstance) { return Promise.resolve(_fireboltInstance); }
    var p = new Promise(function (resolve) { _connectionResolvers.push(resolve); });
    if (!_connecting) {
      console.log("Initiating transport connection...");
      _connecting = true;
      var t = __transport;
      console.log("Registering transport onMessage` callback...");
      t.onMessage(_onMessage);
      console.log("Registering transport onConnectionStatus callback...");
      t.onConnectionStatus(_onStatus);
      console.log("Calling transport.connect()...");
      t.connect();
    }
    return p;
  }


  var _fsm = Object.freeze({
    version: _VERSION,
    transport: _transport,
    get: _get,
  });
  Object.defineProperty(global, "FireboltServiceManager", {
    value: _fsm,
    writable: false,
    configurable: false,
    enumerable: true,
  });

})(typeof globalThis !== "undefined" ? globalThis : window);