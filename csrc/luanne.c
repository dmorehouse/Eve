#include <runtime.h>
#include <luanne.h>
#include <http/http.h>

extern string edb_dump(heap h, edb b);

static void lua_stack_dump (lua_State *L) {
    int i;
    int top = lua_gettop(L);
    for (i = 1; i <= top; i++) {  /* repeat for each level */
        int t = lua_type(L, i);
        switch (t) {

        case LUA_TSTRING:  /* strings */
            printf("%d. `%s'", i, lua_tostring(L, i));
            break;

        case LUA_TBOOLEAN:  /* booleans */
            printf("%d. %s", i, lua_toboolean(L, i) ? "true" : "false");
            break;

        case LUA_TNUMBER:  /* numbers */
            printf("%d. %g", i, lua_tonumber(L, i));
            break;

        default:  /* other values */
            printf("%d. %s", i, lua_typename(L, t));
            break;

        }
        printf(",  ");  /* put a separator */
    }
    printf("\n");  /* end the listing */
}

static char *luat(lua_State *L, int index)
{
    return (char *)lua_typename(L, lua_type(L, index));
}

#define foreach_lua_table(__L, __ind, __k, __v) \
    lua_pushnil(__L); \
    for (int __k = -2, __v = - 1; (lua_next(__L, __ind<0?__ind-1:__ind) != 0) ; lua_pop(__L, 1))


buffer lua_to_buffer(lua_State *L, int index, heap h)
{
    int len = lua_strlen(L, index);
    buffer b = allocate_buffer(h, len);
    memcpy(bref(b, 0), (void *)lua_tostring(L, index), len);
    b->end = len;
    return b;
}

value lua_tovalue(lua_State *L, int index)
{
    switch(lua_type(L, index)){
    case LUA_TBOOLEAN:
        return lua_toboolean(L, index)?etrue:efalse;
    case LUA_TNUMBER:
        // our number isn't making it into float space
        // also our heap is getting mixed up with luas
        return box_float(lua_tonumber(L, index));
    case LUA_TSTRING:
        return intern_string((void *)lua_tostring(L, index),  lua_strlen(L, index));
    case LUA_TLIGHTUSERDATA:
        //presumably from us
        return lua_touserdata(L, index);
    default:
        // figure out how to signal a lua error
        printf("yeah, sorry, i dont eat that kind of stuff %s\n", lua_typename(L, lua_type(L, index)));
    }
    return 0;
}


static int construct_uuid(lua_State *L)
{
    interpreter c = lua_context(L);
    unsigned char *body  = (void *)lua_tostring(L, 1);
    unsigned int length = lua_strlen(L, 1);
    // probably move this out of luanne
    unsigned char id[12];

    if (length > 24) {
        lua_pushnil(L);
        return 1;
    }

    for (int i = 0 ; i < length; i += 2) {
        int loc = i/2;
        id[loc] = (digit_of(body[i]) * 16) + digit_of(body[i+1]);
    }
    lua_pushlightuserdata(L, intern_uuid(id));
    return 1;
}


static int construct_register(lua_State *L)
{
    evaluation c = (void *)lua_context(L);
    int offset = (int)lua_tonumber(L, 1);
    lua_pushlightuserdata(L, (void *)(register_base + offset));
    return 1;
}

static int node_id(lua_State *L)
{
    node n = lua_touserdata(L, 1);
    lua_pushnumber(L, (int)*((double *)n->id));
    return (1);
}

static int construct_number(lua_State *L)
{
    interpreter c = lua_context(L);
    char *s = (char *)lua_tostring(L, 1);
    int len = lua_strlen(L, 1);
    boolean fractional = false;
    double rez = 0, fact = (s[0]=='-')?(s++, len--, -1.0):1.0;

    for (int i = 0; i < len ; i++) {
        if (s[i] == '.'){
            fractional = true;
        } else {
            if (fractional) fact /= 10.0f;
            rez = rez * 10.0f + (double)digit_of(s[i]);
        }
    }

    lua_pushlightuserdata(L, box_float(rez * fact));
    return 1;
}

static int construct_boolean(lua_State *L)
{
    interpreter c = lua_context(L);
    int x = lua_toboolean(L,1);
    lua_pushlightuserdata(L, x?etrue:efalse);
    return 1;
}

static int construct_string(lua_State *L)
{
    lua_pushlightuserdata(L, intern_string((void *)lua_tostring(L, 1), lua_strlen(L, 1)));
    return 1;
}

static int lua_print_value(lua_State *L)
{
    void *x = lua_touserdata(L, 1);
    interpreter c = lua_context(L);
    string out = allocate_string(c->h);
    print_value(out, x);
    lua_pushlstring(L, bref(out, 0), buffer_length(out));
    return 1;
}

static int lua_gen_uuid(lua_State *L)
{
    lua_pushlightuserdata(L, generate_uuid());
    return 1;
}


void luaL_traceback (lua_State *L, lua_State *L1, const char *msg,
                     int level);

static int traceback(lua_State *L)
{
  if (!lua_isstring(L, 1)) { /* Non-string error object? Try metamethod. */
    if (lua_isnoneornil(L, 1) ||
        !luaL_callmeta(L, 1, "__tostring") ||
        !lua_isstring(L, -1))
      return 1;  /* Return non-string error object. */
    lua_remove(L, 1);  /* Replace object by result of __tostring metamethod. */
  }
  luaL_traceback(L, L, lua_tostring(L, 1), 1);
  return 1;
}

void require_luajit(interpreter c, char *z)
{
    lua_pushcfunction(c->L, traceback);
    lua_getglobal(c->L, "require");
    lua_pushlstring(c->L, z, cstring_length(z));
    if (lua_pcall(c->L, 1, 1, lua_gettop(c->L)-2)) {
        printf ("lua error\n");
        printf ("%s\n", lua_tostring(c->L, -1));
    }
    lua_setglobal(c->L, z);
}

vector lua_compile_eve(interpreter c, heap h, buffer b, boolean tracing, bag *compiler_bag)
{
    vector result = allocate_vector(h, 3);
    lua_pushcfunction(c->L, traceback);
    lua_getglobal(c->L, "compiler");
    lua_getfield(c->L, -1, "compileExec");
    lua_pushlstring(c->L, bref(b, 0), buffer_length(b));
    lua_pushboolean(c->L, tracing);

    if (lua_pcall(c->L, 2, 2, lua_gettop(c->L)-4)) {
        printf ("lua error\n");
        printf ("%s\n", lua_tostring(c->L, -1));
    }

    *compiler_bag = lua_tovalue(c->L, 5);

    int count = 0;
    foreach_lua_table(c->L, 4, k, v) {
        compiled n = allocate(c->h, sizeof(struct compiled));
        foreach_lua_table(c->L, v, k0, v0) {
            value kv = lua_tovalue(c->L, k0);
            // xxx - do we have a direct extract?
            if (kv == sym(name)) n->name = lua_tovalue(c->L, v0);
            if (kv == sym(regs)) n->regs = (int)lua_tonumber(c->L, v0);
            if (kv == sym(head)) n->head = lua_tovalue(c->L, v0);
        }
        vector_insert(result, n);
    }
    lua_pop(c->L, 1);
    return(result);
}

value lua_run_module_func(interpreter c, buffer b, char *module, char *func)
{
    lua_pushcfunction(c->L, traceback);
    require_luajit(c, module);
    lua_getglobal(c->L, module);
    lua_getfield(c->L, -1, func);
    lua_pushlstring(c->L, bref(b, 0), buffer_length(b));
    if (lua_pcall(c->L, 1, 1, lua_gettop(c->L)-3)) {
        printf ("lua error\n");
        printf ("%s\n", lua_tostring(c->L, -1));
    }
    return lua_tovalue(c->L, -1);
}

void lua_run(interpreter c, buffer b)
{
    int r;
    lua_pushcfunction(c->L, traceback);
    if ((r= luaL_loadbuffer(c->L, b->contents, buffer_length(b), ""))){
        printf ("lua load error %d\n", r);
    } else {
        if (lua_pcall(c->L, 0, 0, lua_gettop(c->L)-1)) {
            printf ("lua error\n");
            printf ("%s\n", lua_tostring(c->L, -1));
        }
        lua_close(c->L);
    }
}

extern int luaopen_utf8(lua_State *L);

extern void bundle_add_loaders(lua_State* L);

vector vector_from_lua(heap h, lua_State *L, int index)
{
    vector res = allocate_vector(h, 5);
    foreach_lua_table(L, index, _, v)
        vector_insert(res, lua_tovalue(L, v));
    return res;
}


int lua_build_node(lua_State *L)
{
    interpreter c = lua_context(L);
    node n = allocate(c->h, sizeof(struct node));
    estring x = lua_tovalue(L, 1);
    n->type = x;
    n->builder = table_find(builders_table(),x) ;

    if (!n->builder) {
        prf ("no such node type: %v\n", x);
    }

    n->arms = vector_from_lua(c->h, L, 2);
    n->arguments = create_value_table(c->h);
    n->display = create_value_table(c->h);

    foreach_lua_table(L, 3, k, v)  {
        table_set(n->arguments, lua_tovalue(c->L, k),
                     (lua_type(L, v) == LUA_TTABLE)?
                  vector_from_lua(c->h, L, v):
                  lua_tovalue(L, v));

        string out = allocate_string(c->h);
        if(lua_type(L, v) == LUA_TTABLE) {
            print_value_vector_json(out, vector_from_lua(c->h, L, v));
        } else {
            print_value_json(out, lua_tovalue(L, v));
        }
        table_set(n->display, lua_tovalue(c->L, k), out);
    }

    // xxx - shouldn't really be a value
    value node_id = lua_tovalue(L, 4);
    n->id = node_id;

    lua_pushlightuserdata(L, n);
    return 1;
}

int lua_create_edb(lua_State *L)
{
    interpreter c = lua_context(L);
    uuid id = lua_tovalue(L, 1);
    bag b = (bag)create_edb(c->h, id, 0);

    lua_pushlightuserdata(L, b);
    return 1;
}

int lua_insert_edb(lua_State *L)
{
    interpreter c = lua_context(L);
    bag b = (bag) lua_touserdata(L, 1);
    value e = (value) lua_touserdata(L, 2);
    value a = (value) lua_touserdata(L, 3);
    value v = (value) lua_touserdata(L, 4);
    int m = (int)lua_tonumber(L, 5);
    apply(b->insert, e, a, v, m, 0);

    return 0;
}

int lua_dump_edb(lua_State *L)
{
    interpreter c = lua_context(L);
    bag b = (bag) lua_touserdata(L, 1);
    string out = edb_dump(c->h, (edb) b);
    lua_pushlstring(L, bref(out, 0), buffer_length(out));

    return 1;
}

// FIXME - CAS thread safety or per core lists
static interpreter freelist = 0;

interpreter build_lua()
{
    heap h = allocate_rolling(pages, sstring("lua"));
    interpreter c = allocate(h, sizeof(struct interpreter));
    c->L = luaL_newstate();
    c->h = h;

    luaL_openlibs(c->L);
    bundle_add_loaders(c->L);

    // make me a lua package ala utf8
    define(c, "suuid", construct_uuid);
    define(c, "snumber", construct_number);
    define(c, "sregister", construct_register);
    define(c, "sboolean", construct_boolean);
    define(c, "sstring", construct_string);
    define(c, "generate_uuid", lua_gen_uuid);
    define(c, "value_to_string", lua_print_value);
    define(c, "build_node", lua_build_node);
    define(c, "node_id", node_id);
    define(c, "create_edb", lua_create_edb);
    define(c, "insert_edb", lua_insert_edb);
    define(c, "dump_edb", lua_dump_edb);
    require_luajit(c, "compiler");
    return c;
}

interpreter get_lua()
{
    interpreter lua;
        // FIXME - CAS threading
    if (freelist) {
        lua = freelist;
        freelist = lua->next;
    } else {
        lua = build_lua();
    }
    return lua;
}

void free_lua(interpreter lua)
{
    // luc_gc(lua->l, LUA_GCCOLLECT, 0);
    lua_settop(lua->L, 1);
    lua->next = freelist;
    freelist = lua;
}

vector compile_eve(heap h, buffer b, boolean tracing, bag *compiler_bag)
{
    interpreter lua = get_lua();
    lua->h = h;
    vector v = lua_compile_eve(lua, h, b, tracing, compiler_bag);
    free_lua(lua);
    return v;
}
