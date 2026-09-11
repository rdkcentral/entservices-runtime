#include "transport.h"
#include "wsclient.h"

#include <utility>
#include <memory>

class ExceptionHandler
{
public:
    ExceptionHandler(JSCContext* context)
        : _context(context)
    {
        jsc_context_push_exception_handler(_context, [](JSCContext* context, JSCException* exception, gpointer) {
            g_warning("Unexpected exception: %s", jsc_exception_get_message(exception));
            jsc_context_clear_exception(context);
        }, nullptr, nullptr);
    }

    ~ExceptionHandler()
    {
        pop();
    }

    void pop()
    {
        jsc_context_pop_exception_handler(_context);
    }

private:
    JSCContext* _context;
};

class BaseTransport
{
protected:
    enum State: int
    {
        Closed  = 1,
        Closing = 2,
        Opening = 3,
        Open    = 4,
    };

    State _state { Closed };

    JSCValue* _on_open_cb { nullptr };
    JSCValue* _on_close_cb { nullptr };
    JSCValue* _on_error_cb { nullptr };
    JSCValue* _on_message_cb { nullptr };

    bool check_callback(JSCValue* cb)
    {
        if (!jsc_value_is_function(cb) &&
            !jsc_value_is_null(cb) &&
            !jsc_value_is_undefined(cb))
        {
            jsc_context_throw(jsc_context_get_current(), "Unexpected type of callback value.");
            return false;
        }
        return true;
    }

public:
    BaseTransport()
    {
        g_debug("BaseTransport: ctor");
    }

    virtual ~BaseTransport()
    {
        g_debug("BaseTransport: Dtor");
        clear();
    }

    void clear()
    {
        g_debug("BaseTransport: clear!");

        g_clear_object(&_on_open_cb);
        g_clear_object(&_on_close_cb);
        g_clear_object(&_on_error_cb);
        g_clear_object(&_on_message_cb);
        _state = Closed;
    }

    int state() const
    {
        return static_cast<int>(_state);
    }

    JSCValue* get_on_open_callback() const
    {
        if (_on_open_cb)
            return g_object_ref(_on_open_cb);
        return jsc_value_new_null(jsc_context_get_current());
    }
    void set_on_open_callback(JSCValue* cb)
    {
        g_return_if_fail(check_callback(cb));
        g_clear_object(&_on_open_cb);
        if (jsc_value_is_function(cb))
        {
            _on_open_cb = g_object_ref(cb);
        }
    }

    JSCValue* get_on_message_callback() const
    {
        if (_on_message_cb)
            return g_object_ref(_on_message_cb);
        return jsc_value_new_null(jsc_context_get_current());
    }
    void set_on_message_callback(JSCValue* cb)
    {
        g_return_if_fail(check_callback(cb));
        g_clear_object(&_on_message_cb);
        if (jsc_value_is_function(cb))
        {
            _on_message_cb = g_object_ref(cb);
        }
    }

    JSCValue* get_on_error_callback() const
    {
        if (_on_error_cb)
            return g_object_ref(_on_error_cb);
        return jsc_value_new_null(jsc_context_get_current());
    }
    void set_on_error_callback(JSCValue* cb)
    {
        g_return_if_fail(check_callback(cb));
        g_clear_object(&_on_error_cb);
        if (jsc_value_is_function(cb))
        {
            _on_error_cb = g_object_ref(cb);
        }
    }

    JSCValue* get_on_close_callback() const
    {
        if (_on_close_cb)
            return g_object_ref(_on_close_cb);
        return jsc_value_new_null(jsc_context_get_current());
    }
    void set_on_close_callback(JSCValue* cb)
    {
        g_return_if_fail(check_callback(cb));
        g_clear_object(&_on_close_cb);
        if (jsc_value_is_function(cb))
        {
            _on_close_cb = g_object_ref(cb);
        }
    }

    virtual void open() = 0;
    virtual void send(JSCValue* message) = 0;
    virtual void close() = 0;
};

class WebSocketTransport final: public BaseTransport
{
private:
    std::unique_ptr<WsClient> _ws;
    std::string _url;
public:
    explicit WebSocketTransport(std::string url)
        : _url(std::move(url))
    {
        g_debug("WebSocketTransport created %p", this);
        s_transport = this;
    }

    ~WebSocketTransport() override
    {
        clear();
        g_debug("WebSocketTransport destroyed %p", this);
    }

    void clear()
    {
        g_debug("WebSocketTransport clear %p", this);
        s_transport = nullptr;
        BaseTransport::clear();
        _ws.reset();
    }

    void open() override
    {
        if (_state != Closed)
        {
            jsc_context_throw_printf(jsc_context_get_current(), "Incorrect state 0x%x.", _state);
            return;
        }

        _state = Opening;
        _ws = std::make_unique<WsClient>();
        _ws->onMessage = [this](GBytes* message) { on_message(message); };
        _ws->onOpen = [this]() { on_open(); };
        _ws->onError= [this](const char* error) { on_error(error); };
        _ws->onClosed = [this]() { on_close(); };
        _ws->connect(_url);
    }

    void on_open()
    {
        if (_state != Opening)
        {
            g_warning("WebSocketTransport unexpected state in on_open 0x%x", _state);
        }
        _state = Open;
        if (_on_open_cb)
        {
            ExceptionHandler handler{jsc_value_get_context(_on_open_cb)};
            auto result = jsc_value_function_call(_on_open_cb, G_TYPE_NONE);
            g_object_unref(result);
        }
    }

    void send(JSCValue* val) override
    {
        if (_state != Open)
        {
            jsc_context_throw(jsc_context_get_current(), "Incorrect state.");
            return;
        }
        if (!jsc_value_is_string(val))
        {
            jsc_context_throw(jsc_context_get_current(), "Value is not a string.");
            return;
        }
        char *message = jsc_value_to_string(val);
        if (message)
        {
            g_debug(">>> %s", message);
            _ws->send(message);
            g_free(message);
        }
    }

    void close() override
    {
        if (_state != Open && _state != Opening)
        {
            jsc_context_throw(jsc_context_get_current(), "Incorrect state.");
            return;
        }
        _state = Closing;
        _ws->disconnect();
        _state = Closed;
    }

    void on_close()
    {
        _state = Closed;
        if (_on_close_cb)
        {
            ExceptionHandler handler{jsc_value_get_context(_on_close_cb)};
            auto result = jsc_value_function_call(_on_close_cb, G_TYPE_NONE);
            g_object_unref(result);
        }
    }

    void on_message(GBytes* message)
    {
        if (_state != Open)
        {
            g_warning("Incorrect state 0x%x.", static_cast<unsigned>(_state));
            return;
        }

        gsize size = 0;
        const gchar* data =
            static_cast<const gchar*>(g_bytes_get_data(message, &size));
        g_debug("<<< %.*s", size, data);
        if (!_on_message_cb)
        {
            g_warning("Unhandled message: '%.*s'", size, data);
            return;
        }

        JSCContext *context = jsc_value_get_context(_on_message_cb);
        ExceptionHandler handler{context};
        auto message_value = jsc_value_new_string_from_bytes(context, message);
        auto result = jsc_value_function_call(_on_message_cb, JSC_TYPE_VALUE, message_value, G_TYPE_NONE);
        if (!jsc_value_is_undefined(result))
        {
            char *result_str = jsc_value_to_string(result);
            g_warning("on_message() returned '%s'", result_str);
            g_free(result_str);
        }
        g_object_unref(result);
        g_object_unref(message_value);
    }

    void on_error(const char* error)
    {
        if (!_on_error_cb)
            return;

        ExceptionHandler handler{jsc_value_get_context(_on_error_cb)};
        auto result = jsc_value_function_call(_on_error_cb, G_TYPE_STRING, error, G_TYPE_NONE);
        g_object_unref(result);

        if (_state > Closing)
        {
            close();
            on_close();
        }
    }

    static WebSocketTransport* s_transport;
};

WebSocketTransport* WebSocketTransport::s_transport = nullptr;

struct TransportClass
{
    static int get_state(BaseTransport* transport)
    {
        return transport->state();
    }

    static void set_onopen(BaseTransport* transport, JSCValue* value)
    {
        transport->set_on_open_callback(value);
    }

    static JSCValue* get_onopen(BaseTransport* transport)
    {
        return transport->get_on_open_callback();
    }

    static void set_onmessage(BaseTransport* transport, JSCValue* value)
    {
        transport->set_on_message_callback(value);
    }

    static JSCValue* get_onmessage(BaseTransport* transport)
    {
        return transport->get_on_message_callback();
    }

    static void set_onclose(BaseTransport* transport, JSCValue* value)
    {
        transport->set_on_close_callback(value);
    }

    static JSCValue* get_onclose(BaseTransport* transport)
    {
        return transport->get_on_close_callback();
    }

    static void set_onerror(BaseTransport* transport, JSCValue* value)
    {
        transport->set_on_error_callback(value);
    }

    static JSCValue* get_onerror(BaseTransport* transport)
    {
        return transport->get_on_error_callback();
    }

    static void call_open(BaseTransport* transport)
    {
        transport->open();
    }

    static void call_send(BaseTransport* transport, JSCValue* message)
    {
        transport->send(message);
    }

    static void call_close(BaseTransport* transport)
    {
        transport->close();
    }

    static BaseTransport *create_instance(std::string url)
    {
        return new WebSocketTransport(url);
    }

    static void destroy_instance(BaseTransport* transport)
    {
        delete transport;
    }
};

static JSCClass* create_transport_class(JSCContext* jsContext)
{
    g_debug("Register class in context: %p", jsContext);

    JSCClass* transport_class = jsc_context_register_class(
        jsContext,
        "Transport",
        nullptr,
        nullptr,
        reinterpret_cast<GDestroyNotify>(&TransportClass::destroy_instance));

    // properties
    jsc_class_add_property(
        transport_class,
        "state",
        G_TYPE_INT,
        G_CALLBACK(&TransportClass::get_state),
        nullptr,
        nullptr,
        nullptr);

    jsc_class_add_property(
        transport_class,
        "onopen",
        JSC_TYPE_VALUE,
        G_CALLBACK(&TransportClass::get_onopen),
        G_CALLBACK(&TransportClass::set_onopen),
        nullptr,
        nullptr);

    jsc_class_add_property(
        transport_class,
        "onmessage",
        JSC_TYPE_VALUE,
        G_CALLBACK(&TransportClass::get_onmessage),
        G_CALLBACK(&TransportClass::set_onmessage),
        nullptr,
        nullptr);

    jsc_class_add_property(
        transport_class,
        "onclose",
        JSC_TYPE_VALUE,
        G_CALLBACK(&TransportClass::get_onclose),
        G_CALLBACK(&TransportClass::set_onclose),
        nullptr,
        nullptr);

    jsc_class_add_property(
        transport_class,
        "onerror",
        JSC_TYPE_VALUE,
        G_CALLBACK(&TransportClass::get_onerror),
        G_CALLBACK(&TransportClass::set_onerror),
        nullptr,
        nullptr);

    // methods
    jsc_class_add_method(
        transport_class,
        "open",
        G_CALLBACK(&TransportClass::call_open),
        nullptr,
        nullptr,
        G_TYPE_NONE,
        0,
        G_TYPE_NONE);

    jsc_class_add_method(
        transport_class,
        "send",
        G_CALLBACK(&TransportClass::call_send),
        nullptr,
        nullptr,
        G_TYPE_NONE,
        1,
        JSC_TYPE_VALUE);

    jsc_class_add_method(
        transport_class,
        "close",
        G_CALLBACK(&TransportClass::call_close),
        nullptr,
        nullptr,
        G_TYPE_NONE,
        0,
        G_TYPE_NONE);

    return transport_class;
}

G_BEGIN_DECLS

JSCValue* create_transport(JSCContext* jsContext, const char* url)
{
    // Keep single connection active, the last one wins
    if (WebSocketTransport::s_transport)
    {
        WebSocketTransport::s_transport->clear();
    }

    JSCClass* transportClass = create_transport_class(jsContext);
    return jsc_value_new_object(
        jsContext,
        TransportClass::create_instance(url),
        transportClass);
}

void clear_transport()
{
    if (WebSocketTransport::s_transport)
    {
        WebSocketTransport::s_transport->clear();
    }    
}

G_END_DECLS
