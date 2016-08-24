#include <runtime.h>

// do we really need this? i mean eventually for reflection purposes
typedef struct process {
    evaluation ev;
} *process;

typedef struct process_bag{
    table process_map;

} *process_bag;

void start_process(table scopes)
{
    session->persisted = create_value_table(h);

    table_set(session->persisted, session->root->u, session->root);
    table_set(session->persisted, session->session->u, session->session);

    session->scopes = create_value_table(session->h);
    table_set(session->scopes, intern_cstring("session"), session->session->u);
    table_set(session->scopes, intern_cstring("all"), session->root->u);
    table_set(session->scopes, intern_cstring("browser"), session->browser_uuid);

}

CONTINUATION_1_1(process_bag_commit, filebag, edb)
void process_bag_commit(filebag fb, edb s)
{
    edb_foreach_a(s, e, sym(scope), v, m) {

    }

    edb_foreach_a(s, e, sym(source), v, m) {
        file parent;
        if ((parent = table_find(fb->idmap, e))){
            allocate_file(fb, parent, v);
        }
    }
}


// not sure if bag is the right model for presenting this interface, but it can be changed
bag process_bag_init(buffer root_pathname, uuid root)
{
    heap h = allocate_rolling(init, sstring("process_bag"));
    process_bag pb = allocate(h, sizeof(struct process_bag));
    pb->h = h;
    pb->b.insert = cont(h, process_bag_insert, pb);
    pb->b.scan = cont(h, process_bag_scan, pb);
    pb->b.u = generate_uuid();
    pb->b.listeners = allocate_table(h, key_from_pointer, compare_pointer);
    pb->b.implications = allocate_table(h, key_from_pointer, compare_pointer);
    pb->b.commit = cont(h, process_bag_commit, pb);
    return (bag)pb;
}
