#include <runtime.h>
#include <http/http.h>
#include <luanne.h>

typedef struct json_session {
    heap h;
    table current_delta;
    uuid u;
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
    apply(session->down->w, out, cont(h, send_destroy, h));
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


    send_diff(h, session->down->w, diff);

    destroy(session->current_delta->h);
    session->current_delta = results;
}

u64 id_bracket_open = 0xe2a691;
u64 id_bracket_close = 0xe2a692;
boolean is_stringy_uuid(value v) {
    if(type_of(v) != estring_space) return false;
    estring s = (estring)v;
    if(s->length < 6) return false; // It's too short to contain id brackets at all.
    u64 open_rune = 0;
    memcpy(&open_rune, s->body, 3);
    if(memcmp(&open_rune, &id_bracket_open, 3) != 0) return false;
    u64 close_rune = 0;
    memcpy(&close_rune, s->body + s->length - 4, 3);
    if(memcmp(&close_rune, &id_bracket_close, 3) != 0) return false;
    if(close_rune != id_bracket_close) return false;
    return true;
}

value map_if_uuid(heap h, value v, bag browser, table mapping) {
    if(!is_stringy_uuid(v)) return v;
    value mapped = table_find(mapping, v);
    if(mapped) return mapped; // If we've already been mapped, reuse that value.

    // Check to see if we map to an existing uuid
    estring s = (estring)v;
    buffer str = alloca_wrap_buffer(s->body, s->length);
    table_foreach(((edb)browser)->eav, e, avl) {
        buffer eid = allocate_buffer(h, 40);
        bprintf(eid , "⦑%X⦒", alloca_wrap_buffer(e, UUID_LENGTH));
        if(string_equal(eid, str)) {
            table_set(mapping, v, e);
            return e;
        }
    }

    // If we don't map at all, map us to a new uuid.
    uuid id = generate_uuid();
    table_set(mapping, v, id);
    return id;
}

static CONTINUATION_1_2(json_input, json_session, bag, uuid);
static void json_input(json_session s, bag json_bag, uuid root_id)
{
    if(!json_bag) {
        // should we kill the evaluation? most likely
        return;
    }

    edb b = (edb)json_bag;
    value type = lookupv(b, root_id, sym(type));
    if(type == sym(event)) {
        table mapping = create_value_table(s->h);
        bag event = (bag)create_edb(s->h, 0);
        bag browser = table_find(s->ev->scopes, sym(browser));
        value eavs_id = lookupv(b, root_id, sym(insert));
        int ix = 1;
        while(true) {
            value eav_id = lookupv(b, eavs_id, box_float(ix));
            if(!eav_id) break;
            value e = map_if_uuid(s->h, lookupv(b, eav_id, box_float(1)), browser, mapping);
            value a = map_if_uuid(s->h, lookupv(b, eav_id, box_float(2)), browser, mapping);
            value v = map_if_uuid(s->h, lookupv(b, eav_id, box_float(3)), browser, mapping);

            apply(event->insert, e, a, v, 1, 0); // @NOTE: It'd be cute to be able to tag this as coming from the json session.
            ix++;
        }
        prf("JSON EVENT\n%b\n", edb_dump(s->h, (edb)event));
        inject_event(s->ev, event);
    }
}

object_handler create_json_session(heap h, evaluation ev, uuid u)
{
    // allocate json parser
    json_session s = allocate(h, sizeof(struct json_session));
    s->h = h;
    s->ev = ev;
    s->u = u;
    return(cont(h, json_input, s));
}
