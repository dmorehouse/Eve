#include <runtime.h>
#include <json_request.h>
#include <http/http.h>
// rfc 2616

struct http_server {
    heap h;
    evaluation ev;
    table sessions;
};
typedef struct session {
    bag b;
    heap h;
    uuid self;
    http_server parent;
    buffer_handler write;
    evaluation ev;
} *session;

void http_send_response(http_server s, bag b, uuid root)
{
    bag shadow = (bag)b;
    estring body;
    uuid connection_uuid = lookupv((edb)b, root, sym(connection));
    uuid response = root;
    session hs = table_find(s->sessions, connection_uuid);

    prf("send response: %p %p\n", response, s);
    if (!s || !response) return;
    // type checking or coersion
    value header = lookupv((edb)b, root, sym(header));

    if ((body = lookupv((edb)b, root, sym(body))) && (type_of(body) == estring_space)) {
        // dont shadow because http header can't handle it because edb_foreach
        //                shadow = (bag)create_edb(hs->h, 0, build_vector(hs->h, s));
        apply(shadow->insert, header, sym(Content-Length), box_float(body->length), 1, 0);
    }

    http_send_header(hs->write, shadow, header,
                     sym(HTTP/1.1),
                     lookupv((edb)shadow, response, sym(status)),
                     lookupv((edb)shadow, response, sym(reason)));
    if (body) {
        // xxx - leak the wrapper
        buffer b = wrap_buffer(hs->h, body->body, body->length);
        apply(hs->write, b, ignore);
    }
}

static CONTINUATION_1_3(dispatch_request, session, bag, uuid, register_read);
static void dispatch_request(session s, bag b, uuid i, register_read reg)
{
    buffer *c;

    if (b == 0){
        // tell evie?
        prf ("http connection shutdown\n");
        destroy(s->h);
        return;
    }

    prf("request: %b %v\n", edb_dump(init, (edb)b), i);

    bag event = (bag)create_edb(s->h, build_vector(s->h, b));
    uuid x = generate_uuid();
    apply(event->insert, x, sym(tag), sym(http-request), 1, 0);
    apply(event->insert, x, sym(request), i, 1, 0);
    apply(event->insert, x, sym(connection), s->self, 1, 0);
    inject_event(s->parent->ev,event);

    apply(reg, request_header_parser(s->h, cont(s->h, dispatch_request, s)));
}

CONTINUATION_1_3(new_connection, http_server, buffer_handler, station, register_read);
void new_connection(http_server s,
                    buffer_handler write,
                    station peer,
                    register_read reg)
{
    heap h = allocate_rolling(pages, sstring("connection"));
    session hs = allocate(h, sizeof(struct session));
    hs->write = write;
    hs->parent = s;
    hs->h = h;
    hs->self = generate_uuid();
    table_set(s->sessions, hs->self, hs);

    // as it stands, no one really cares about new connects arriving,
    // but it seems at minumum we might want to log and keep track
    apply(reg, request_header_parser(h, cont(h, dispatch_request, hs)));
}


http_server create_http_server(station p, evaluation ev)
{
    heap h = allocate_rolling(pages, sstring("server"));
    http_server s = allocate(h, sizeof(struct http_server));

    s->h = h;
    s->ev = ev;
    s->sessions = create_value_table(h);

    tcp_create_server(h,
                      p,
                      cont(h, new_connection, s),
                      ignore);
    return(s);
}
