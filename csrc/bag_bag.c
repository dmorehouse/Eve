#include <runtime.h>

CONTINUATION_1_1(bagbag_commit, filebag, edb)
void bagbag_commit(evaluation ev, edb s)
{
    edb_foreach_av(s, e, sym(tag), sym(bag), m) {
        bag b = (bag)create_edb();
        table_insert(ev->t_input, e, m);
    }

    edb_foreach_a(s, e, sym(name), v, m) {
        // we're going to silent refuse to bind fruits into the bag namespace?
        // maybe this map should be raw eavs?
        bag b;
        if (table_find(ev->inputs, e)) {
            table_insert(eb->scopes, v, e);
        }
    }
}

CONTINUATION_1_5(bagbag_scan, evaluation, int, listener, value, value, value);
void bagbag_scan(evaluation ev, int sig, listener out, value e, value a, value v)
{
    if (sig & e_sig) {
    }
    if (sig & a_sig) {
    }
    if (sig & v_sig) {
        
    }
}

bag init_bag_bag(evaluation ev)
{
    heap h = allocate_rolling(init, sstring("process_bag"));
    bag_bag pb = allocate(h, sizeof(struct process_bag));
    bb->h = h;
    bb->b.insert = cont(h, process_bag_insert, bb);
    bb->b.scan = cont(h, process_bag_scan, bb);
    bb->b.u = generate_uuid(); // xxx - probably well known
    bb->b.listeners = allocate_table(h, key_from_pointer, compare_pointer);
    bb->b.implications = allocate_table(h, key_from_pointer, compare_pointer);
    bb->b.commit = cont(h, process_bag_commit, bb);
    return (bag)bb;
}
