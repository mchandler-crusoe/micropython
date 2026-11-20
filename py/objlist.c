/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>
#include <assert.h>

#include "py/objlist.h"
#include "py/runtime.h"
#include "py/cstack.h"

static mp_obj_t mp_obj_new_list_iterator(mp_obj_t list, size_t cur, mp_obj_iter_buf_t *iter_buf);
static mp_obj_list_t *list_new(size_t n);
static mp_obj_t list_extend(mp_obj_t self_in, mp_obj_t arg_in);
static mp_obj_t list_pop(size_t n_args, const mp_obj_t *args);

// TODO: Move to mpconfig.h
#define LIST_MIN_ALLOC 4

/******************************************************************************/
/* list                                                                       */

static void list_print(const mp_print_t *print, mp_obj_t o_in, mp_print_kind_t kind) {
    mp_obj_list_t *o = MP_OBJ_TO_PTR(o_in);
    const char *item_separator = ", ";
    if (!(MICROPY_PY_JSON && kind == PRINT_JSON)) {
        kind = PRINT_REPR;
    } else {
        #if MICROPY_PY_JSON_SEPARATORS
        item_separator = MP_PRINT_GET_EXT(print)->item_separator;
        #endif
    }
    mp_print_str(print, "[");
    for (size_t i = 0; i < o->len; i++) {
        if (i > 0) {
            mp_print_str(print, item_separator);
        }
        mp_obj_print_helper(print, o->items[i], kind);
    }
    mp_print_str(print, "]");
}

static mp_obj_t list_extend_from_iter(mp_obj_t list, mp_obj_t iterable) {
    mp_obj_t iter = mp_getiter(iterable, NULL);
    mp_obj_t item;
    while ((item = mp_iternext(iter)) != MP_OBJ_STOP_ITERATION) {
        mp_obj_list_append(list, item);
    }
    return list;
}

mp_obj_t mp_obj_list_make_new(const mp_obj_type_t *type_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type_in;
    mp_arg_check_num(n_args, n_kw, 0, 1, false);

    switch (n_args) {
        case 0:
            // return a new, empty list
            return mp_obj_new_list(0, NULL);

        case 1:
        default: {
            // make list from iterable
            // TODO: optimize list/tuple
            mp_obj_t list = mp_obj_new_list(0, NULL);
            return list_extend_from_iter(list, args[0]);
        }
    }
}

static mp_obj_t list_unary_op(mp_unary_op_t op, mp_obj_t self_in) {
    mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
    switch (op) {
        case MP_UNARY_OP_BOOL:
            return mp_obj_new_bool(self->len != 0);
        case MP_UNARY_OP_LEN:
            return MP_OBJ_NEW_SMALL_INT(self->len);
        #if MICROPY_PY_SYS_GETSIZEOF
        case MP_UNARY_OP_SIZEOF: {
            size_t sz = sizeof(*self) + sizeof(mp_obj_t) * self->alloc;
            return MP_OBJ_NEW_SMALL_INT(sz);
        }
        #endif
        default:
            return MP_OBJ_NULL;      // op not supported
    }
}

static mp_obj_t list_binary_op(mp_binary_op_t op, mp_obj_t lhs, mp_obj_t rhs) {
    mp_obj_list_t *o = MP_OBJ_TO_PTR(lhs);
    switch (op) {
        case MP_BINARY_OP_ADD: {
            if (!mp_obj_is_type(rhs, &mp_type_list)) {
                return MP_OBJ_NULL; // op not supported
            }
            mp_obj_list_t *p = MP_OBJ_TO_PTR(rhs);
            mp_obj_list_t *s = list_new(o->len + p->len);
            mp_seq_cat(s->items, o->items, o->len, p->items, p->len, mp_obj_t);
            return MP_OBJ_FROM_PTR(s);
        }
        case MP_BINARY_OP_INPLACE_ADD: {
            list_extend(lhs, rhs);
            return lhs;
        }
        case MP_BINARY_OP_MULTIPLY: {
            mp_int_t n;
            if (!mp_obj_get_int_maybe(rhs, &n)) {
                return MP_OBJ_NULL; // op not supported
            }
            if (n < 0) {
                n = 0;
            }
            mp_obj_list_t *s = list_new(o->len * n);
            mp_seq_multiply(o->items, sizeof(*o->items), o->len, n, s->items);
            return MP_OBJ_FROM_PTR(s);
        }
        case MP_BINARY_OP_EQUAL:
        case MP_BINARY_OP_LESS:
        case MP_BINARY_OP_LESS_EQUAL:
        case MP_BINARY_OP_MORE:
        case MP_BINARY_OP_MORE_EQUAL: {
            if (!mp_obj_is_type(rhs, &mp_type_list)) {
                if (op == MP_BINARY_OP_EQUAL) {
                    return mp_const_false;
                }
                return MP_OBJ_NULL; // op not supported
            }

            mp_obj_list_t *another = MP_OBJ_TO_PTR(rhs);
            bool res = mp_seq_cmp_objs(op, o->items, o->len, another->items, another->len);
            return mp_obj_new_bool(res);
        }

        default:
            return MP_OBJ_NULL; // op not supported
    }
}

static mp_obj_t list_subscr(mp_obj_t self_in, mp_obj_t index, mp_obj_t value) {
    #if MICROPY_PY_BUILTINS_SLICE
    if (mp_obj_is_type(index, &mp_type_slice)) {
        mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
        mp_bound_slice_t slice;
        bool fast = mp_seq_get_fast_slice_indexes(self->len, index, &slice);
        if (value == MP_OBJ_SENTINEL) {
            // load
            if (!fast) {
                return mp_seq_extract_slice(self->items, &slice);
            }
            mp_obj_list_t *res = list_new(slice.stop - slice.start);
            mp_seq_copy(res->items, self->items + slice.start, res->len, mp_obj_t);
            return MP_OBJ_FROM_PTR(res);
        }
        // assign/delete
        if (value == MP_OBJ_NULL) {
            // delete is equivalent to slice assignment of an empty sequence
            value = mp_const_empty_tuple;
        }
        if (!fast) {
            mp_raise_NotImplementedError(NULL);
        }
        size_t value_len;
        mp_obj_t *value_items;
        mp_obj_get_array(value, &value_len, &value_items);
        mp_int_t len_adj = value_len - (slice.stop - slice.start);
        if (len_adj > 0) {
            if (self->len + len_adj > self->alloc) {
                // TODO: Might optimize memory copies here by checking if block can
                // be grown inplace or not
                self->items = m_renew(mp_obj_t, self->items, self->alloc, self->len + len_adj);
                self->alloc = self->len + len_adj;
            }
            mp_seq_replace_slice_grow_inplace(self->items, self->len,
                slice.start, slice.stop, value_items, value_len, len_adj, sizeof(*self->items));
        } else {
            mp_seq_replace_slice_no_grow(self->items, self->len,
                slice.start, slice.stop, value_items, value_len, sizeof(*self->items));
            // Clear "freed" elements at the end of list
            mp_seq_clear(self->items, self->len + len_adj, self->len, sizeof(*self->items));
            // TODO: apply allocation policy re: alloc_size
        }
        self->len += len_adj;
        return mp_const_none;
    }
    #endif
    if (value == MP_OBJ_NULL) {
        // delete
        mp_obj_t args[2] = {self_in, index};
        list_pop(2, args);
        return mp_const_none;
    } else if (value == MP_OBJ_SENTINEL) {
        // load
        mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
        size_t index_val = mp_get_index(self->base.type, self->len, index, false);
        return self->items[index_val];
    } else {
        mp_obj_list_store(self_in, index, value);
        return mp_const_none;
    }
}

static mp_obj_t list_getiter(mp_obj_t o_in, mp_obj_iter_buf_t *iter_buf) {
    return mp_obj_new_list_iterator(o_in, 0, iter_buf);
}

mp_obj_t mp_obj_list_append(mp_obj_t self_in, mp_obj_t arg) {
    mp_check_self(mp_obj_is_type(self_in, &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->len >= self->alloc) {
        self->items = m_renew(mp_obj_t, self->items, self->alloc, self->alloc * 2);
        self->alloc *= 2;
        mp_seq_clear(self->items, self->len + 1, self->alloc, sizeof(*self->items));
    }
    self->items[self->len++] = arg;
    return mp_const_none; // return None, as per CPython
}

static mp_obj_t list_extend(mp_obj_t self_in, mp_obj_t arg_in) {
    mp_check_self(mp_obj_is_type(self_in, &mp_type_list));
    if (mp_obj_is_type(arg_in, &mp_type_list)) {
        mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
        mp_obj_list_t *arg = MP_OBJ_TO_PTR(arg_in);

        if (self->len + arg->len > self->alloc) {
            // TODO: use alloc policy for "4"
            self->items = m_renew(mp_obj_t, self->items, self->alloc, self->len + arg->len + 4);
            self->alloc = self->len + arg->len + 4;
            mp_seq_clear(self->items, self->len + arg->len, self->alloc, sizeof(*self->items));
        }

        memcpy(self->items + self->len, arg->items, sizeof(mp_obj_t) * arg->len);
        self->len += arg->len;
    } else {
        list_extend_from_iter(self_in, arg_in);
    }
    return mp_const_none; // return None, as per CPython
}

static mp_obj_t list_pop(size_t n_args, const mp_obj_t *args) {
    mp_check_self(mp_obj_is_type(args[0], &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->len == 0) {
        mp_raise_msg(&mp_type_IndexError, MP_ERROR_TEXT("pop from empty list"));
    }
    size_t index = mp_get_index(self->base.type, self->len, n_args == 1 ? MP_OBJ_NEW_SMALL_INT(-1) : args[1], false);
    mp_obj_t ret = self->items[index];
    self->len -= 1;
    memmove(self->items + index, self->items + index + 1, (self->len - index) * sizeof(mp_obj_t));
    // Clear stale pointer from slot which just got freed to prevent GC issues
    self->items[self->len] = MP_OBJ_NULL;
    if (self->alloc > LIST_MIN_ALLOC && self->alloc > 2 * self->len) {
        self->items = m_renew(mp_obj_t, self->items, self->alloc, self->alloc / 2);
        self->alloc /= 2;
    }
    return ret;
}

// list.sort() is a stable, in-place natural merge sort.
//
// The list is split into natural runs: non-descending ones, and strictly
// descending ones which are reversed in place (being strict, this keeps the
// sort stable).  Runs are merged in a balanced binary pattern by sort_runs(),
// and sort_merge() merges two runs without a buffer by rotating items into
// place.  So already sorted or reversed input takes n - 1 comparisons, and
// otherwise a sort takes O(n log n) comparisons and O(n log^2 n) moves.
//
// Items are only ever swapped, so if a comparison or the key function raises
// then the list still holds every original item.
//
// C stack usage is O(log n): sort_runs() recurses at most log2(n) + 1 deep
// (see below), and sort_merge() recurses only into a merge whose left range is
// at most half as long, so at most log2(n) deep.

typedef struct _sort_t {
    mp_obj_t key_fn;
    bool reverse;
    mp_obj_t *end;
} sort_t;

static mp_obj_t sort_key(const sort_t *s, mp_obj_t x) {
    return s->key_fn == MP_OBJ_NULL ? x : mp_call_function_1(s->key_fn, x);
}

// Compare two keys.  For reverse the operands are swapped, rather than the
// result negated, so that equal items still keep their original order.
static bool sort_lt_keys(const sort_t *s, mp_obj_t a, mp_obj_t b) {
    if (s->reverse) {
        mp_obj_t t = a;
        a = b;
        b = t;
    }
    return mp_binary_op(MP_BINARY_OP_LESS, a, b) == mp_const_true;
}

static bool sort_lt(const sort_t *s, mp_obj_t a, mp_obj_t b) {
    return sort_lt_keys(s, sort_key(s, a), sort_key(s, b));
}

#if MICROPY_OPT_SORT_PIVOT_KEY
// Compute the key of the merge pivot once for its whole binary search.
#define sort_pivot(s, x) sort_key(s, x)
#define sort_lt_pivot(s, x, v) sort_lt_keys(s, sort_key(s, x), v)
#else
#define sort_pivot(s, x) (x)
#define sort_lt_pivot(s, x, v) sort_lt(s, x, v)
#endif

static void sort_reverse(mp_obj_t *lo, mp_obj_t *hi) {
    while (lo < --hi) {
        mp_obj_t t = *lo;
        *lo++ = *hi;
        *hi = t;
    }
}

// Merge the sorted ranges [a, m) and [m, b) in place.
static void sort_merge(const sort_t *s, mp_obj_t *a, mp_obj_t *m, mp_obj_t *b) {
    mp_cstack_check();
    while (a < m && m < b) {
        // Take the middle item of the left range as the pivot and binary search
        // the right range for the first item not less than it.
        mp_obj_t *p = a + (m - a) / 2;
        mp_obj_t v = sort_pivot(s, *p);
        mp_obj_t *lo = m;
        mp_obj_t *hi = b;
        while (lo < hi) {
            mp_obj_t *mid = lo + (hi - lo) / 2;
            if (sort_lt_pivot(s, *mid, v)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        // Rotate [p, m) and [m, lo) to put the pivot in its final place at nm.
        sort_reverse(p, m);
        sort_reverse(m, lo);
        sort_reverse(p, lo);
        mp_obj_t *nm = p + (lo - m);
        // Merge the items before the pivot, whose left range is [a, p), half
        // as long as before, then loop to merge the items after it.
        if (a < p && p < nm) {
            sort_merge(s, a, p, nm);
        }
        a = nm + 1;
        m = nm + (m - p);
    }
}

// Find the natural run starting at a, then merge onto it the following
// 2^0, 2^1, ..., 2^(depth - 1) runs, each group sorted by a recursive call.
// Return the end of the sorted range.  A call at depth d sorts at most 2^d
// runs, so the top-level call never passes more than log2(n) + 1 down.
static mp_obj_t *sort_runs(const sort_t *s, mp_obj_t *a, size_t depth) {
    mp_cstack_check();
    mp_obj_t *end = s->end;
    mp_obj_t *b = a + 1;
    if (b < end) {
        bool desc = sort_lt(s, *b, *a);
        while (++b < end && sort_lt(s, *b, b[-1]) == desc) {
        }
        if (desc) {
            sort_reverse(a, b);
        }
    }
    for (size_t d = 0; d < depth && b < end; ++d) {
        mp_obj_t *c = sort_runs(s, b, d);
        sort_merge(s, a, b, c);
        b = c;
    }
    return b;
}

mp_obj_t mp_obj_list_sort(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_key, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_reverse, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    // parse args
    struct {
        mp_arg_val_t key, reverse;
    } args;
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, (mp_arg_val_t *)&args);

    mp_check_self(mp_obj_is_type(pos_args[0], &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    if (self->len > 1) {
        sort_t s = {
            args.key.u_obj == mp_const_none ? MP_OBJ_NULL : args.key.u_obj,
            args.reverse.u_bool,
            self->items + self->len,
        };
        sort_runs(&s, self->items, (size_t)-1);
    }

    return mp_const_none;
}

static mp_obj_t list_clear(mp_obj_t self_in) {
    mp_check_self(mp_obj_is_type(self_in, &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
    self->len = 0;
    self->items = m_renew(mp_obj_t, self->items, self->alloc, LIST_MIN_ALLOC);
    self->alloc = LIST_MIN_ALLOC;
    mp_seq_clear(self->items, 0, self->alloc, sizeof(*self->items));
    return mp_const_none;
}

static mp_obj_t list_copy(mp_obj_t self_in) {
    mp_check_self(mp_obj_is_type(self_in, &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_list(self->len, self->items);
}

static mp_obj_t list_count(mp_obj_t self_in, mp_obj_t value) {
    mp_check_self(mp_obj_is_type(self_in, &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_seq_count_obj(self->items, self->len, value);
}

static mp_obj_t list_index(size_t n_args, const mp_obj_t *args) {
    mp_check_self(mp_obj_is_type(args[0], &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(args[0]);
    return mp_seq_index_obj(self->items, self->len, n_args, args);
}

static mp_obj_t list_insert(mp_obj_t self_in, mp_obj_t idx, mp_obj_t obj) {
    mp_check_self(mp_obj_is_type(self_in, &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);
    // insert has its own strange index logic
    mp_int_t index = MP_OBJ_SMALL_INT_VALUE(idx);
    if (index < 0) {
        index += self->len;
    }
    if (index < 0) {
        index = 0;
    }
    if ((size_t)index > self->len) {
        index = self->len;
    }

    mp_obj_list_append(self_in, mp_const_none);

    for (mp_int_t i = self->len - 1; i > index; i--) {
        self->items[i] = self->items[i - 1];
    }
    self->items[index] = obj;

    return mp_const_none;
}

mp_obj_t mp_obj_list_remove(mp_obj_t self_in, mp_obj_t value) {
    mp_check_self(mp_obj_is_type(self_in, &mp_type_list));
    mp_obj_t args[] = {self_in, value};
    args[1] = list_index(2, args);
    list_pop(2, args);

    return mp_const_none;
}

static mp_obj_t list_reverse(mp_obj_t self_in) {
    mp_check_self(mp_obj_is_type(self_in, &mp_type_list));
    mp_obj_list_t *self = MP_OBJ_TO_PTR(self_in);

    mp_int_t len = self->len;
    for (mp_int_t i = 0; i < len / 2; i++) {
        mp_obj_t a = self->items[i];
        self->items[i] = self->items[len - i - 1];
        self->items[len - i - 1] = a;
    }

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_2(list_append_obj, mp_obj_list_append);
static MP_DEFINE_CONST_FUN_OBJ_2(list_extend_obj, list_extend);
static MP_DEFINE_CONST_FUN_OBJ_1(list_clear_obj, list_clear);
static MP_DEFINE_CONST_FUN_OBJ_1(list_copy_obj, list_copy);
static MP_DEFINE_CONST_FUN_OBJ_2(list_count_obj, list_count);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(list_index_obj, 2, 4, list_index);
static MP_DEFINE_CONST_FUN_OBJ_3(list_insert_obj, list_insert);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(list_pop_obj, 1, 2, list_pop);
static MP_DEFINE_CONST_FUN_OBJ_2(list_remove_obj, mp_obj_list_remove);
static MP_DEFINE_CONST_FUN_OBJ_1(list_reverse_obj, list_reverse);
static MP_DEFINE_CONST_FUN_OBJ_KW(list_sort_obj, 1, mp_obj_list_sort);

static const mp_rom_map_elem_t list_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_append), MP_ROM_PTR(&list_append_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&list_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy), MP_ROM_PTR(&list_copy_obj) },
    { MP_ROM_QSTR(MP_QSTR_count), MP_ROM_PTR(&list_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_extend), MP_ROM_PTR(&list_extend_obj) },
    { MP_ROM_QSTR(MP_QSTR_index), MP_ROM_PTR(&list_index_obj) },
    { MP_ROM_QSTR(MP_QSTR_insert), MP_ROM_PTR(&list_insert_obj) },
    { MP_ROM_QSTR(MP_QSTR_pop), MP_ROM_PTR(&list_pop_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&list_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_reverse), MP_ROM_PTR(&list_reverse_obj) },
    { MP_ROM_QSTR(MP_QSTR_sort), MP_ROM_PTR(&list_sort_obj) },
};

static MP_DEFINE_CONST_DICT(list_locals_dict, list_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_list,
    MP_QSTR_list,
    MP_TYPE_FLAG_ITER_IS_GETITER,
    make_new, mp_obj_list_make_new,
    print, list_print,
    unary_op, list_unary_op,
    binary_op, list_binary_op,
    subscr, list_subscr,
    iter, list_getiter,
    locals_dict, &list_locals_dict
    );


void mp_obj_list_init(mp_obj_list_t *o, size_t n) {
    o->base.type = &mp_type_list;
    o->alloc = n < LIST_MIN_ALLOC ? LIST_MIN_ALLOC : n;
    o->len = n;
    o->items = m_new(mp_obj_t, o->alloc);
    mp_seq_clear(o->items, n, o->alloc, sizeof(*o->items));
}

static mp_obj_list_t *list_new(size_t n) {
    mp_obj_list_t *o = m_new_obj(mp_obj_list_t);
    mp_obj_list_init(o, n);
    return o;
}

mp_obj_t mp_obj_new_list(size_t n, mp_obj_t *items) {
    mp_obj_list_t *o = list_new(n);
    if (items != NULL) {
        for (size_t i = 0; i < n; i++) {
            o->items[i] = items[i];
        }
    }
    return MP_OBJ_FROM_PTR(o);
}

/******************************************************************************/
/* list iterator                                                              */

typedef struct _mp_obj_list_it_t {
    mp_obj_base_t base;
    mp_fun_1_t iternext;
    mp_obj_t list;
    size_t cur;
} mp_obj_list_it_t;

static mp_obj_t list_it_iternext(mp_obj_t self_in) {
    mp_obj_list_it_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_list_t *list = MP_OBJ_TO_PTR(self->list);
    if (self->cur < list->len) {
        mp_obj_t o_out = list->items[self->cur];
        self->cur += 1;
        return o_out;
    } else {
        return MP_OBJ_STOP_ITERATION;
    }
}

mp_obj_t mp_obj_new_list_iterator(mp_obj_t list, size_t cur, mp_obj_iter_buf_t *iter_buf) {
    assert(sizeof(mp_obj_list_it_t) <= sizeof(mp_obj_iter_buf_t));
    mp_obj_list_it_t *o = (mp_obj_list_it_t *)iter_buf;
    o->base.type = &mp_type_polymorph_iter;
    o->iternext = list_it_iternext;
    o->list = list;
    o->cur = cur;
    return MP_OBJ_FROM_PTR(o);
}

mp_obj_list_t *mp_obj_list_optional_arg(mp_obj_t arg_in, size_t min_len) {
    if (arg_in == MP_OBJ_NULL || arg_in == mp_const_none) {
        return MP_OBJ_TO_PTR(mp_obj_new_list(min_len, NULL));
    } else {
        return mp_obj_list_ensure(arg_in, min_len);
    }
}

mp_obj_list_t *mp_obj_list_ensure(mp_obj_t in, size_t min_len) {
    if (!mp_obj_is_type(in, &mp_type_list)) {
        mp_raise_TypeError(NULL);
    }
    mp_obj_list_t *list = MP_OBJ_TO_PTR(in);
    if (list->len < min_len) {
        mp_raise_ValueError(NULL);
    }
    return list;
}
