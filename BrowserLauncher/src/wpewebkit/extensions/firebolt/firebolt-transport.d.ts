/**
 * Type definitions for window.__firebolt_transport__
 *
 * The WPE Firebolt web extension injects this frozen, read-only object into
 * the main frame's JavaScript context. It provides a low-level WebSocket
 * transport bridge to the Firebolt backend service.
 *
 * Every method requires a `clientId` token as its first argument. The token
 * is delivered to the page's JavaScript SDK via
 * `FireboltServiceManager.configure({ clientId })` before this object is
 * available.
 */

// ---------------------------------------------------------------------------
// Result & Error types
// ---------------------------------------------------------------------------

/**
 * Error codes returned in {@link FireboltTransportResult.errorCode} when a
 * method call fails.
 */
export declare const enum FireboltTransportErrorCode {
    /** The native page-state object has not been initialised. */
    PAGE_STATE_UNAVAILABLE = 1001,
    /** Required parameters are missing or of the wrong type. */
    INVALID_PARAMETERS = 1002,
    /** The `clientId` argument could not be read. */
    PAGE_STATE_CLIENT_ID_MISSING = 1003,
    /** The supplied `clientId` does not match the page token. */
    CLIENT_ID_MISMATCH = 1004,
}

/**
 * The return type of all {@link FireboltTransport} methods and unsubscribe
 * functions.
 *
 * On success:  `{ success: true }`
 * On failure:  `{ success: false, errorCode: FireboltTransportErrorCode }`
 */
export type FireboltTransportResult =
    | { success: true }
    | { success: false; errorCode: FireboltTransportErrorCode };

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------

/**
 * Callback invoked when the WebSocket connection state changes.
 *
 * @param status - `"connected"` when the transport has connected to the Firebolt
 *   endpoint; `"disconnected"` when it has disconnected.
 */
export type FireboltConnectionStatusCallback = (status: "connected" | "disconnected") => void;

/**
 * Callback invoked for each text frame received from the Firebolt backend.
 *
 * @param message - Raw text payload (typically a JSON-RPC response string).
 */
export type FireboltMessageCallback = (message: string) => void;

/**
 * A zero-argument function that removes a previously registered listener.
 * Returns a {@link FireboltTransportResult} indicating whether the removal
 * succeeded.
 */
export type FireboltUnsubscribeFn = () => FireboltTransportResult;

// ---------------------------------------------------------------------------
// Transport interface
// ---------------------------------------------------------------------------

/**
 * Low-level Firebolt transport bridge exposed as `window.__firebolt_transport__`.
 *
 * The object is **frozen** and **non-configurable** — it cannot be replaced,
 * extended, or deleted from JavaScript.
 *
 * All methods authenticate the caller via the `clientId` token that was
 * delivered to `FireboltServiceManager.configure({ clientId })`.
 */
export interface FireboltTransport {
    /**
     * Opens a WebSocket connection to the Firebolt endpoint.
     *
     * If the transport is already connected, the call is a safe no-op.
     * The connection is established **asynchronously** — a successful return
     * only means the attempt was initiated. Register a listener via
     * {@link onConnectionStatus} to know when the connection is ready.
     *
     * @param clientId - The token received in `FireboltServiceManager.configure`.
     * @returns `{ success: true }` when the attempt was started, or
     *   `{ success: false, errorCode }` on authorisation failure.
     */
    connect(clientId: string): FireboltTransportResult;

    /**
     * Closes the active WebSocket connection.
     *
     * Safe to call when not connected.
     *
     * @param clientId - The token received in `FireboltServiceManager.configure`.
     * @returns `{ success: true }` on success, or
     *   `{ success: false, errorCode }` on authorisation failure.
     */
    disconnect(clientId: string): FireboltTransportResult;

    /**
     * Sends a text message over the active WebSocket connection.
     *
     * The message is silently dropped if the transport is not connected.
     *
     * @param clientId - The token received in `FireboltServiceManager.configure`.
     * @param message  - The text payload to transmit (typically a JSON-RPC
     *   request string).
     * @returns `{ success: true }` when the message was passed to the WebSocket
     *   layer, or `{ success: false, errorCode }` on authorisation failure.
     */
    send(clientId: string, message: string): FireboltTransportResult;

    /**
     * Subscribes to WebSocket connection-state change events.
     *
     * The callback is invoked on the JS main thread each time the transport
     * connects or disconnects.
     *
     * Multiple independent subscribers may be registered; each is removed
     * individually by calling its returned unsubscribe function.
     *
     * @param clientId - The token received in `FireboltServiceManager.configure`.
     * @param callback - Receives `"connected"` on connect and `"disconnected"` on disconnect.
     * @returns An {@link FireboltUnsubscribeFn unsubscribe function} that, when
     *   called, stops the callback from receiving further events.
     *
     * @example
     * ```ts
     * const off = transport.onConnectionStatus(clientId, (status) => {
     *   console.log('connected:', status === 'connected');
     * });
     * // later…
     * off();
     * ```
     */
    onConnectionStatus(
        clientId: string,
        callback: FireboltConnectionStatusCallback
    ): FireboltUnsubscribeFn;

    /**
     * Subscribes to incoming messages from the Firebolt backend.
     *
     * The callback is invoked on the JS main thread for every text frame
     * received over the WebSocket.
     *
     * Multiple independent subscribers may be registered; each is removed
     * individually by calling its returned unsubscribe function.
     *
     * @param clientId - The token received in `FireboltServiceManager.configure`.
     * @param callback - Receives the raw text payload of each incoming frame.
     * @returns An {@link FireboltUnsubscribeFn unsubscribe function} that, when
     *   called, stops the callback from receiving further messages.
     *
     * @example
     * ```ts
     * const off = transport.onMessage(clientId, (msg) => {
     *   const response = JSON.parse(msg);
     *   // handle JSON-RPC response…
     * });
     * // later…
     * off();
     * ```
     */
    onMessage(
        clientId: string,
        callback: FireboltMessageCallback
    ): FireboltUnsubscribeFn;
}

// ---------------------------------------------------------------------------
// Global augmentation
// ---------------------------------------------------------------------------

declare global {
    interface Window {
        /**
         * Frozen, read-only Firebolt transport bridge injected by the WPE
         * Firebolt web extension.
         *
         * Available only after the extension has called
         * `FireboltServiceManager.configure({ clientId })` and completed
         * page initialisation.
         *
         * @see {@link FireboltTransport}
         */
        readonly __firebolt_transport__: FireboltTransport;
    }
}
