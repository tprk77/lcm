

#ifndef TPRK77_LUALCM_HASH_H
#define TPRK77_LUALCM_HASH_H

#include "lua.h"
#include "stdint.h"

/**
 * @page Hash object related functions.
 *
 * @see ll_hash_makemetatable
 * @see ll_hash_register_new
 */

/* utility functions */
void ll_hash_makemetatable(lua_State *);
void ll_hash_register_new(lua_State *);

/* some more utility */
void ll_hash_fromvalue(lua_State *, uint64_t);
uint64_t ll_hash_tovalue(lua_State *, int);

#endif
