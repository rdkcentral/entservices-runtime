(function () {
    'use strict';
    let _initPromise;
    return function (createBridgeObject) {
        // ServiceManager public surface
        return {
            version: 2.2,
            getServiceForJavaScript: function (name, readyCb) {
                if (name !== 'com.comcast.BridgeObject_1') {
                    console.error('SM: Requested service("' + name + '") is not supported.');
                    return;
                }
                if (!_initPromise) {
                    _initPromise = new Promise(function(resolve) {
                        createBridgeObject(
                            // ready callback
                            resolve,
                            // badger result callback
                            function(pid, success, json) {
                                if (typeof window.$badger?.callback === 'function')
                                    window.$badger.callback(pid !== undefined ? pid : null, success, json);
                            },
                            // badger event callback
                            function(handlerId, json) {
                                if (typeof window.$badger?.event === 'function')
                                    window.$badger.event(handlerId, json);
                            }
                        );
                    }).catch(err) {
                        console.error('SM: could not create requested service "' + name + '".', err);
                    };
                }
                _initPromise.then(readyCb);
            }
        }
    }
})();
