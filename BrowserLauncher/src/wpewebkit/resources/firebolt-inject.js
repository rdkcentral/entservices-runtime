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
    "Accessibility.VoiceGuidanceSettings": {"kind":"object","properties":{"enabled":{"kind":"primitive","type":"bool"},"rate":{"kind":"primitive","type":"number","constraints":{"minimum":0.1,"maximum":10}},"navigationHints":{"kind":"primitive","type":"bool"}},"required":["enabled","rate","navigationHints"]},
    "Discovery.AgePolicy": {"kind":"enum","values":["app:adult","app:child","app:teen"]}
  };
  
  var _methodRegistry = {
    "Accessibility.voiceGuidanceSettings": {"kind":"call","paramsSchema":null,"resultSchema":{"kind":"ref","name":"Accessibility.VoiceGuidanceSettings"}},
    "Discovery.watched": {"kind":"call","paramsSchema":{"kind":"object","properties":{"entityId":{"kind":"primitive","type":"string"},"progress":{"kind":"optional","inner":{"kind":"primitive","type":"number"}},"completed":{"kind":"optional","inner":{"kind":"primitive","type":"bool"}},"watchedOn":{"kind":"optional","inner":{"kind":"primitive","type":"string"}},"agePolicy":{"kind":"optional","inner":{"kind":"ref","name":"Discovery.AgePolicy"}}},"required":["entityId"]},"resultSchema":{"kind":"null"}},
    "Localization.onCountryChanged": {"kind":"subscribe","eventIsPrimitive":true,"eventSchema":{"kind":"primitive","type":"string","constraints":{"minLength":2,"maxLength":2,"pattern":"^[A-Z]{2}$"}}}
  };
  
  // --- End generated data ---

  // ---------------------------------------------------------------------------
  // Transport layer
  // ---------------------------------------------------------------------------
  function _onMessage(raw) {
    var message;
    try { message = JSON.parse(raw); } catch (e) { return; }

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
    __transport = transport;
  }

  function _get() {
    if (!__transport) {
      throw new Error(
        "FireboltServiceManager.get() called before configure(). " +
        "The WPE extension must call configure({ clientId }) first."
      );
    }
    if (_connected && _fireboltInstance) { return Promise.resolve(_fireboltInstance); }
    var p = new Promise(function (resolve) { _connectionResolvers.push(resolve); });
    if (!_connecting) {
      _connecting = true;
      var t = __transport;
      t.onMessage(_onMessage);
      t.onConnectionStatus(_onStatus);
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