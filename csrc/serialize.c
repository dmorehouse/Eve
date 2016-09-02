#include <runtime.h>

// the serialization typespace
#define uuid_bits 0x80
#define uuid_mask 0x7f

#define string_bits 0x20
#define string_mask 0x20

#define float_bits 0x13
#define float_mask 0x00

/*
 1xxxxxxx uuid   // remember to steal this bit
 01xxxxxx string // followed by length encoded as - 0xxxx immedate (32 bytes :()
 001xxxxx ??
 00010000 bigdec // not a thing
 00000    constant space - see below
 00000100 version zero // ?
 00000011 triple marker // doesn't need to be a whole btye
 00000010 float64 // - unless we do the denorm trick, then this guy would likely be 00000000
 00000001 true
 00000000 false
*/

/*
 * note on denormal numbers - highest 11 bits are zero
 */

#define version_zero 0x08
#define triple_marker 0x03
#define float64_prelude 0x02
#define true_constant 0x01
#define false_constant 0x0

void serialize_value(buffer dest, value v)
{

    switch(type_of(v)) {
    case register_space:
    case estring_space:
    case float_space:
    case uuid_space:
    }
}

void serialize_edb(buffer dest, edb db)
{
    edb_foreach(db, e, a, v, m) {
        buffer_write_byte(dest);
    }
}


typedef struct deserialize {
    closure(handler, value);
    buffer partial;
    unsigned char basetype;
    unsigned int length;
    unsigned long target;
} *deserialize;

static value constant_translation(byte z)
{
}

CONTINUATION_1_1(deserialize d, buffer b);
static void deserialize_input(deserialize d, buffer b)
{
    buffer intermediate;
    unsigned long target_type;
    if (length == 0) {
        switch(target_type) {
        case uuid_space:
            apply(intern_uuid(bref(d->partial, 0)));

        }
        buffer_clear(d->partial);
    }

}


buffer_handler allocate_deserialize(heap h, closure(handler,value))
{


}
