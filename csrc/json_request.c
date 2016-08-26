#include <runtime.h>
#include <json_request.h>
#include <http/http.h>
#include <luanne.h>

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

// solution should already contain the diffs against persisted...except missing support (diane)
static CONTINUATION_1_3(send_response, json_session, multibag, multibag, table);
static void send_response(json_session session, multibag t_solution, multibag f_solution, table counters)
{
    heap h = allocate_rolling(pages, sstring("response"));
    heap p = allocate_rolling(pages, sstring("response delta"));
    table results = create_value_vector_table(p);
    edb browser;

    if (f_solution && (browser = table_find(f_solution, session->browser_uuid))) {
        edb_foreach(browser, e, a, v, c, _)
            table_set(results, build_vector(p, e, a, v), etrue);
    }


    values_diff diff = diff_value_vector_tables(p, session->current_delta, results);
    // destructs h

    if (t_solution && (browser = table_find(t_solution, session->browser_uuid))) {
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

// this should be a reflection
void send_parse(json_session session, buffer query)
{
    heap h = allocate_rolling(pages, sstring("parse response"));
    string out = allocate_string(h);
    interpreter lua = get_lua();
    value json = lua_run_module_func(lua, query, "parser", "parseJSON");
    estring json_estring = json;
    buffer_append(out, json_estring->body, json_estring->length);
    free_lua(lua);
    // send the json message
    apply(session->write, out, cont(h, send_destroy, h));
}


CONTINUATION_1_2(handle_json_query, json_session, bag, uuid);
void handle_json_query(json_session session, bag in, uuid root)
{
    if (in == 0) {
        close_evaluation(session->ev);
        destroy(session->h);
        return;
    }

    // in is going to take some deconstruction here, but you know....stuff
    inject_event(session->ev, in);
}


void new_json_session(evaluation ev,
                      buffer_handler write,
                      bag b,
                      uuid u,
                      register_read reg)
{
    heap h = allocate_rolling(pages, sstring("session"));
    uuid su = generate_uuid();

    json_session session = allocate(h, sizeof(struct json_session));
    session->graph = 0; // @FIXME: remove this completely
    session->h = h;
    session->session = (bag)create_edb(h, 0);
    session->current_delta = create_value_vector_table(allocate_rolling(pages, sstring("trash")));
    session->browser_uuid = generate_uuid();
    session->eh = allocate_rolling(pages, sstring("eval"));


    session->write = websocket_send_upgrade(session->eh, b, u,
                                      write,
                                      parse_json(session->eh, cont(h, handle_json_query, session)),
                                      reg);

}
