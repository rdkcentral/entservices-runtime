/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <wpe/webkit-web-extension.h>

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/uio.h>


// -----------------------------------------------------------------------------
/*!
    \brief WPEWebKit extension / plugin to write a web page's console.log output
    to stdout.

    When build the library (libLogExtension.so) needs to be in a
    sub-directory within the widget called "extensions".  From there WPEWebKit
    will load the extension when the browser instance starts.

 */


// -----------------------------------------------------------------------------
/*!
    \internal

    Writes the given console.log message onto stdout.

 */
static void logToConsole(WebKitConsoleMessageLevel level, const char *fileName,
                         unsigned lineNum, const char *message)
{
    struct timespec ts = { 0, 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);

    int n = 0;
    struct iovec iov[6];

    iov[n].iov_base = (char*)("console.log: ");
    iov[n].iov_len = 13;
    n++;

    char tbuf[32];
    iov[n].iov_base = tbuf;
    iov[n].iov_len = snprintf(tbuf, sizeof(tbuf), "%.010lu.%.06lu ",
                              ts.tv_sec, ts.tv_nsec / 1000);
    iov[n].iov_len = MIN(iov[n].iov_len, sizeof(tbuf));
    n++;

    switch (level)
    {
        case WEBKIT_CONSOLE_MESSAGE_LEVEL_ERROR:
            iov[n].iov_base = (void*)"ERR: ";
            iov[n].iov_len = 5;
            break;
        case WEBKIT_CONSOLE_MESSAGE_LEVEL_WARNING:
            iov[n].iov_base = (void*)"WRN: ";
            iov[n].iov_len = 5;
            break;
        case WEBKIT_CONSOLE_MESSAGE_LEVEL_LOG:
            iov[n].iov_base = (void*)"LOG: ";
            iov[n].iov_len = 5;
            break;
        case WEBKIT_CONSOLE_MESSAGE_LEVEL_INFO:
            iov[n].iov_base = (void*)"NFO: ";
            iov[n].iov_len = 5;
            break;
        case WEBKIT_CONSOLE_MESSAGE_LEVEL_DEBUG:
            iov[n].iov_base = (void*)"DBG: ";
            iov[n].iov_len = 5;
            break;
        default:
            iov[n].iov_base = (void*)": ";
            iov[n].iov_len = 2;
            break;
    }
    n++;

    if (fileName)
    {
        char fbuf[156];
        iov[n].iov_base = (void *) fbuf;
        iov[n].iov_len = snprintf(fbuf, sizeof(fbuf), "< S:%.*s L:%u > ",
                                  64, fileName ?: "?",
                                  lineNum);
        iov[n].iov_len = MIN(iov[n].iov_len, sizeof(fbuf));
        n++;
    }

    if (message)
    {
        iov[n].iov_base = (char *)(message);
        iov[n].iov_len = strlen(message);
        n++;
    }

    iov[n].iov_base = (void*)"\n";
    iov[n].iov_len = 1;
    n++;

    writev(STDOUT_FILENO, iov, n);
}


// -----------------------------------------------------------------------------
/*!
    \internal

    Callback for when the current browser page notifies us of a
    'console-message-sent' event.

    \see https://webkitgtk.org/reference/webkit2gtk/stable/WebKitWebPage.html
 */
static void onConsoleMessageSent(WebKitWebPage *page,
                                 WebKitConsoleMessage *message,
                                 gpointer userData)
{
    (void) page;
    (void) userData;

    WebKitConsoleMessageLevel level = webkit_console_message_get_level(message);

    guint line = webkit_console_message_get_line(message);
    const gchar *file = webkit_console_message_get_source_id(message);
    const gchar *text = webkit_console_message_get_text(message);

    logToConsole(level, file, line, text);
}

// -----------------------------------------------------------------------------
/*!
    \internal

    Writes the given console.log message details to stdout

 */
static void onWebPageCreated(WebKitWebExtension *extension,
                             WebKitWebPage *page, gpointer userData)
{
    (void) extension;

    g_signal_connect(page, "console-message-sent",
                     G_CALLBACK(onConsoleMessageSent),
                     userData);
}

G_BEGIN_DECLS
    // -------------------------------------------------------------------------
    /*!
        Entry point for the WPEWebKit extension.

        \see  https://webkitgtk.org/reference/webkit2gtk/stable/WebKitWebExtension.html

     */
    G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(WebKitWebExtension *extension,
                                                                        GVariant *userData)
    {
        g_signal_connect(extension, "page-created",
                         G_CALLBACK(onWebPageCreated),
                         NULL);
    }
G_END_DECLS
