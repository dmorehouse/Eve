#include <runtime.h>

typedef u8 byte;

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

/* there should be a 'this' uuid for container metadata..files and connections */

/*
 * note on denormal numbers - highest 11 bits are zero
 */

#define type_uuid 0x80

#define triple_marker 0x03
#define float64_prelude 0x02
#define true_constant 0x01
#define false_constant 0x0

static inline int first_bit_set(u64 value)
{
    return(64-__builtin_clzll(value));
}

static inline u64 mask(int x)
{
    return((1<<x) -1);
}

static inline byte extract(u64 source, int highest_start, int bits)
{
    return (source >> (highest_start)) & ((1<<bits) -1);
}

static inline void encode_integer(buffer dest, int offset, byte base, u64 value)
{
    unsigned int len = first_bit_set(value);
    int space = 7 - offset;
    buffer_write_byte(dest, (base << offset) | extract(value, len, space));
    while ((len -= space) > 0) {
        buffer_write_byte(dest, ((len > 7)?0x80:0) | extract(value, len, 7));
        space = 7;
    }
}


static inline boolean decode_integer(buffer source, int offset, u64 *value)
{
    u64 result = 0;
    int index = 0;
    int blen = buffer_length(source);
    while (index < blen) {
        byte b = *(byte *)bref(source, index);
        result = (result << offset) | (b & ((1<<offset) -1));
        if (!(b & (1<<(offset + 1)))) {
            *value = result;
            source->start += index;
            return true;
        }
        offset = 7;
    }
    return false;
}

void serialize_value(buffer dest, value v)
{

    switch(type_of(v)) {
    case register_space:
    case estring_space:
    case float_space:
    case uuid_space:
        break;
    }
}

void serialize_edb(buffer dest, edb db)
{
    edb_foreach(db, e, a, v, m, u) {
        /*
         * seems excessive to frame every triple, but not being able to detect
         * synch loss in a dense encoding is a real weak point, imagine throwing
         * in a synch with a count every once and a while
         *
         * we would also like to compress using runs of E and EA, or i guess A
         */
        serialize_value(dest, e);
        serialize_value(dest, a);
        serialize_value(dest, v);
    }
}

typedef struct deserialize {
    closure(handler, value);
    buffer partial;
    value (*translate)(); // saves some switches, wasted some time
    unsigned char basetype;
    unsigned int length;
    unsigned long target;
} *deserialize;

static value produce_efalse() {return efalse;}
static value produce_etrue() {return etrue;}

CONTINUATION_1_1(deserialize_input, deserialize, buffer b);
static void deserialize_input(deserialize d, buffer b)
{
    while (1) {
        // find the object and the length
        while (d->partial || b) {
            if (d->partial) {
                // if we dont know the length, we'll try a byte at a time, its sad, but n
                // should be quite small here, and there are ways to shortcut this if
                // there is a really a problem. just trying to avoid expanding partial
                // and copying b just for a lousy length field
                if (basetype == 0) {
                    move(d->partial, b);
                }
            } else {
                d->partial = b;
                b = 0;
            }

            byte z = bref(d->partial, 0);


            if (z & 80) {
                length = 11;
                // uuid case
            } else {
                if (z & 40) {
                    u64 length;
                    if (!decode_integer(b, 2, &d->length)) return false;
                    // string case
                } else {
                    if (z == float_prelude) {
                        length = 8;
                    }
                }
            }
            if (length == 0) {
                switch(target_type) {
                case uuid_space:
                    apply(intern_uuid(bref(d->partial, 0)));

                }
                buffer_clear(d->partial);
            }
        }
    }
}


buffer_handler allocate_deserialize(heap h, closure(handler,value))
{


}
