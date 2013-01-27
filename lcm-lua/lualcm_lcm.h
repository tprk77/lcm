

#ifndef TPRK77_LCM_LCM_H
#define TPRK77_LCM_LCM_H

#include "lua.h"

/**
 * @page LCM object related functions.
 *
 * @see ll_lcm_makemetatable
 * @see ll_lcm_register_new
 */

void ll_lcm_makemetatable(lua_State *);
void ll_lcm_register_new(lua_State *);

#endif

