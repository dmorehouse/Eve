#include <runtime.h>
#include <http/http.h>
#include <luanne.h>

typedef struct json_session {
    heap h;
    table current_delta;
    uuid u;
    reader self;
    evaluation ev;
    endpoint down;
} *json_session;

buffer format_error_json(heap h, char* message, bag data, uuid data_id);

static CONTINUATION_1_0(send_destroy, heap);
static void send_destroy(heap h)
{
    destroy(h);
}

static void format_vector(buffer out, vector v)
{
    int start = 0;
    vector_foreach(v, i){
        int count = 0;
        if (start++ != 0) bprintf(out, ",");
        bprintf(out, "[");
        vector_foreach(i, j){
            print_value_json(out, j);
            if (count ++ < 2) {
                bprintf(out, ",  ");
            }
        }
        bprintf(out, "]");
    }
}

buffer format_error_json(heap h, char* message, bag data, uuid data_id)
{
    string stack = allocate_string(h);
    get_stack_trace(&stack);

    uuid id = generate_uuid();
    vector includes = allocate_vector(h, 1);
    if(data != 0) {
      vector_set(includes, 0, data);
    }
    bag response = (bag)create_edb(h, includes);
    uuid root = generate_uuid();
    apply(response->insert, root, sym(type), sym(error), 1, 0);
    apply(response->insert, root, sym(stage), sym(executor), 1, 0);
    apply(response->insert, root, sym(message), intern_cstring(message), 1, 0);
    apply(response->insert, root, sym(offsets), intern_buffer(stack), 1, 0);
    if(data != 0) {
      apply(response->insert, root, sym(data), data_id, 1, 0);
    }
    return json_encode(h, response, root);
}

static CONTINUATION_1_3(handle_error, json_session, char *, bag, uuid);
static void handle_error(json_session session, char * message, bag data, uuid data_id) {
    heap h = allocate_rolling(pages, sstring("error handler"));
    buffer out = format_error_json(h, message, data, data_id);
    apply(session->write, out, cont(h, send_destroy, h));
}


// always call this guy independent of commit so that we get an update,
// even on empty, after the first evaluation. warning, destroys
// his heap
static void send_diff(heap h, buffer_handler output, values_diff diff)
{
    string out = allocate_string(h);
    bprintf(out, "{\"type\":\"result\", \"insert\":[");
    format_vector(out, diff->insert);
    bprintf(out, "], \"remove\": [");
    format_vector(out, diff->remove);
    bprintf(out, "]}");
    apply(output, out, cont(h, send_destroy, h));
}

static CONTINUATION_1_2(send_response, json_session, multibag, multibag);
static void send_response(json_session session, multibag t_solution, multibag f_solution)
{
    heap h = allocate_rolling(pages, sstring("response"));
    heap p = allocate_rolling(pages, sstring("response delta"));
    table results = create_value_vector_table(p);
    edb browser;

    if (f_solution && (browser = table_find(f_solution, session->u))) {
        edb_foreach(browser, e, a, v, c, _)
            table_set(results, build_vector(p, e, a, v), etrue);
    }


    values_diff diff = diff_value_vector_tables(p, session->current_delta, results);
    // destructs h

    if (t_solution && (browser = table_find(t_solution, session->u))) {
        edb_foreach(browser, e, a, v, m, u) {
            if (m > 0)
                vector_insert(diff->insert, build_vector(h, e, a, v));
            if (m < 0)
                vector_insert(diff->remove, build_vector(h, e, a, v));
        }
    }


    send_diff(h, session->write, diff);

    destroy(session->current_delta->h);
    session->current_delta = results;
}

static CONTINUATION_1_2(json_input, json_session, bag, uuid);
static void json_input(json_session s, bag b, uuid x)
{
    // uhh, guys?
}

void create_json_session(heap h, evaluation ev, uuid u, endpoint e)
{
    // allocate json parser
    json_session s = allocate(h, sizeof(struct json_session));
    s->h = h;
    s->ev = ev;
    s->u = u;
    s->down = e;
    s->self = cont(h, json_input, s);
    parse_json(heap h, endpoint e, object_handler j);
    apply(e->r, self);
}

