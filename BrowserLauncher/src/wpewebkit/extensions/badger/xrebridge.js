(function () {
 'use strict';

 const DEFAULT_CALL_TIMEOUT_MS = 30000;
 const RECONNECT_DELAY_ON_CLOSE_MS = 3000;
 const REQUEST_TYPE = Object.freeze({
  BADGER: 1,
  JSONRPC: 2,
  JSONRPC_LISTENER: 3
 });

 // Builtin WebSocket transport
 var _transport = null;

 // Serial message id
 var _idCounter = 1;

 // Map<id, PendingCall>
 // where PendingCall: { requestType, timeoutHandle, meta: Map<originalId|pid> }
 var _pendingCalls = Object.create(null);

 // Reconnect timer handle
 var _reconnectTimer = null;

 // Callbacks
 var _resultCallback = null;
 var _eventCallback = null;
 var _readyCallback = null;

 // Protocol detection
 function isJsonRpcRequest(req) {
  return (
   req.jsonrpc === '2.0' &&
   typeof req.method === 'string' &&
   typeof req.id === 'number'
  );
 }
 function isJsonRpcEventListenerRequest(req) {
  return (
   typeof req.params === 'object' &&
   req.params?.listen === true
  );
 }
 function isBadgerRequest(req) {
  return (typeof req.action === 'string');
 }

 // Timeout helpers
 function armTimeout(id, delayMs) {
  return setTimeout(function () {
   var call = _pendingCalls[id];
   if (!call)
    return;
   delete _pendingCalls[id];
   console.warn('xrebridge: call id ' + id + ' timed out');
   try { invokeErrorCallback(call, -32070, 'timed out waiting for reply'); }
   catch (e) { /* swallow */ }
  }, delayMs);
 }

 // Callback helpers
 function invokeBadgerCallback(pid, success, json) {
  _resultCallback(pid, success, json);
 }

 function invokeBadgerEvent(method, params) {
  // params must have { handlerId: string, json: object }
  if (typeof params !== 'object' || params === null) {
   console.warn('xrebridge: badger event params invalid');
   return;
  }
  var handlerId = params.handlerId;
  var json = params.json;
  if (typeof handlerId !== 'string' || typeof json !== 'object' || json === null) {
   console.warn('xrebridge: badger event missing handlerId or json fields');
   return;
  }
  _eventCallback(handlerId, json);
 }

 function dispatchError(call, code, message) {
  if (call.requestType === REQUEST_TYPE.BADGER) {
   invokeBadgerCallback(call.meta.pid, false, {
    code: errCode,
    message: errMessage
   });
  } else {
   invokeBadgerCallback(null, false, {
    jsonrpc: '2.0',
    error: { code: errCode, message: errMessage },
    id: call.meta.originalId
   });
  }
 }

 // Core method call dispatcher
 function methodCall(method, params, requestType, meta) {
  if (_transport.state !== 4) {
   throw "transport is not open";
  }
  var id = _idCounter++;
  var message = JSON.stringify(
   params === undefined
    ? { jsonrpc: '2.0', id: id, method: method }
    : { jsonrpc: '2.0', id: id, method: method, params: params }
  );
  _transport.send(message);
  var timeoutHandle = armTimeout(id, DEFAULT_CALL_TIMEOUT_MS);
  _pendingCalls[id] = {
   requestType,
   timeoutHandle,
   meta
  };
 }

 // Fail all in-flight calls with an error
 function failAllCalls(reason) {
  var calls = _pendingCalls;
  _pendingCalls = Object.create(null);
  var ids = Object.keys(calls);
  for (var j = 0; j < ids.length; j++) {
   var call = calls[ids[j]];
   clearTimeout(call.timeoutHandle);
   try { dispatchError(call, -32603, reason); } catch (e) { /* swallow */ }
  }
 }

 // Inbound message dispatch
 function onJsonRpcResponse(result, id) {
  var call = _pendingCalls[id];
  if (!call) {
   console.warn('xrebridge: received reply for unknown id', id);
   return;
  }
  clearTimeout(call.timeoutHandle);
  if (call.requestType !== REQUEST_TYPE.JSONRPC_LISTENER) {
   delete _pendingCalls[id];
  }
  if (call.requestType === REQUEST_TYPE.BADGER) {
   invokeBadgerCallback(call.meta.pid, true, result);
  } else {
   invokeBadgerCallback(null, true, {
    jsonrpc: '2.0',
    result,
    id: call.meta.originalId
   });
  }
 }

 function onJsonRpcError(error, id) {
  if (typeof id !== 'number') {
   console.warn('xrebridge: received error for unknown id ' + id);
   return;
  }
  var call = _pendingCalls[id];
  if (!call) {
   console.warn('xrebridge: received error for unknown id ' + id);
   return;
  }
  var code = (error && typeof error.code === 'number') ? error.code : -32603;
  var message = (error && typeof error.message === 'string') ? error.message : 'unknown error';
  clearTimeout(call.timeoutHandle);
  delete _pendingCalls[id];
  dispatchError(call, code, message);
 }

 function onJsonRpcNotification(method, params) {
  invokeBadgerEvent(method, params);
 }

 function onTransportMessage(data) {
  var msg = JSON.parse(data);
  if (typeof msg !== 'object' || msg === null || msg.jsonrpc !== '2.0') {
   throw 'received non-JSON-RPC-2.0 message';
  }
  if ('error' in msg) {
   onJsonRpcError(msg.error, msg.id);
  } else if ('method' in msg) {
   onJsonRpcNotification(msg.method, msg.params);
  } else if ('result' in msg) {
   onJsonRpcResponse(msg.result, msg.id);
  } else {
   throw 'received unrecognized JSON-RPC message';
  }
 }

 function onJSMessageChanged(jsonString) {
  var request = JSON.parse(jsonString);
  if (typeof request !== 'object' || request === null) {
   console.warn('xrebridge: JSMessageChanged received non-object');
   return;
  }
  if (isJsonRpcRequest(request)) {
   // ---- JSON-RPC 2.0 path ----
   var originalId = request.id;
   var requestType = isJsonRpcEventListenerRequest(request)
    ? REQUEST_TYPE.JSONRPC_LISTENER : REQUEST_TYPE.JSONRPC;
   methodCall(
    request.method,
    request.params,
    requestType,
    {originalId}
   );
  } else if (isBadgerRequest(request)) {
   // ---- Legacy $badger path ----
   var pid = ('pid' in request) ? request.pid : -1;
   var action = request.action;
   methodCall(
    'badger.' + action,
    request.args,
    REQUEST_TYPE.BADGER,
    {pid}
   );
  } else {
   console.warn('xrebridge: JSMessageChanged received malformed request(not JSON-RPC 2.0 or $badger format)');
  }
 }

 // Transport
 function scheduleReconnect(delayMs) {
  if (_reconnectTimer)
   return;
  _reconnectTimer = setTimeout(function () {
   _reconnectTimer = null;
   _transport.open();
  }, delayMs);
 }

 function connect() {
  _transport.onopen = function () {
   console.info('xrebridge: transport connected');
   if (_readyCallback) {
    _readyCallback({JSMessageChanged: onJSMessageChanged});
    _readyCallback = null;
   }
  };
  _transport.onmessage = function (txt) {
   onTransportMessage(txt);
  };
  _transport.onclose = function () {
   // Fail all in-flight calls
   failAllCalls('transport closed');
   let delay = RECONNECT_DELAY_ON_CLOSE_MS;
   console.warn('xrebridge: disconnected, reconnecting in ' + delay + ' ms');
   scheduleReconnect(delay);
  };
  _transport.open();
 }

 return function(transport, readyCallback, resultCallback, eventCallback) {
  _transport = transport;
  _resultCallback = resultCallback;
  _eventCallback = eventCallback;
  _readyCallback = readyCallback;
  connect();
 }
})();
