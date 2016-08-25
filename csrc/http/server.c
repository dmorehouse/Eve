#include <runtime.h>
#include <json_request.h>
#include <http/http.h>
// rfc 2616

struct http_server {
    heap h;
    evaluation ev;
    // set of sessions?
};

typedef struct session {
    bag b;
    heap h;
    http_server parent;
    buffer_handler write;
    evaluation ev;
} *session;

// where does this guy end up?
//static CONTINUATION_1_3(handle_error, session, char *, bag, uuid);
//static void handle_error(session session, char * message, bag data, uuid data_id) {
//    heap h = allocate_rolling(pages, sstring("error handler"));
//    buffer out = format_error_json(h, message, data, data_id);
//    send_http_response(h, session->write, "500 Internal Server Error", string_from_cstring(h, "application/json"), out);
//    destroy(h);
//}


static CONTINUATION_1_3(http_request_complete, session, multibag, multibag, table);
static void http_request_complete(session hs, multibag f_solution, multibag t_solution, table counters)
{
    edb s = table_find(t_solution, table_find(hs->ev->scopes, sym(session)));

    if (s) {
        edb_foreach_e(s, e, sym(tag), sym(http-response), m) {
            bag shadow = (bag)s;
            estring body;
            // type checking or coersion
            value header = lookupv(s, e, sym(header));
            if ((body = lookupv(s, e, sym(body))) && (type_of(body) == estring_space)) {
                // dont shadow because http header can't handle it
                //                shadow = (bag)create_edb(hs->h, 0, build_vector(hs->h, s));
                apply(shadow->insert, header, sym(Content-Length), box_float(body->length), 1, 0);
            }
            http_send_header(hs->write, shadow, header,
                             sym(HTTP/1.1),
                             lookupv((edb)shadow, e, sym(status)),
                             lookupv((edb)shadow, e, sym(reason)));
            if (body) {
                // xxx - leak
                buffer b = wrap_buffer(hs->h, body->body, body->length);
                apply(hs->write, b, ignore);
            }
        }
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

    inject_event(s->parent->ev,
                 aprintf(s->h,"init!\n```\nbind\n[#http-request request:%v]\n```",
                         i),
                 false);

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

    tcp_create_server(h,
                      p,
                      cont(h, new_connection, s),
                      ignore);
    return(s);
}
