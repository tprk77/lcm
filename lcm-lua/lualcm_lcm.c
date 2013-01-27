
#include "lualcm_lcm.h"
#include "lauxlib.h"
#include "lcm/lcm.h"
#include "sys/select.h"
#include "lua_ref_helper.h"
#include "lua_ver_helper.h"

/** @file */

/*
 * The LCM userdata has a table which holds subscription related data for
 * each subscription created. Each table entry consists of a userdata
 * (to hold a lcm_subscription_t pointer) and handler function. The entries
 * are indexed by a unique integer, generated by luaX_ref.
 *
 * luaX_ref is similar to luaL_ref, except it stores the reference list in
 * a separate table. (luaL_ref stores a reference list in the table being
 * referenced into. This causes problems when trying to iterate over the
 * table.)
 *
 * When a new subscription is created, a new table is created to hold the
 * subscription details. A new subscription userdata is created, which contains
 * a lcm_subscription_t pointer, and pushed onto that table. The hanlder
 * function is also pushed onto the table. The table is then pushed onto the
 * LCM userdata's subscriptions table, which is stored either in the
 * userdata's environment (Lua 5.1) or the uservalue (Lua 5.2). The table is
 * added to the subscription table using luaX_ref.
 *
 * When the subscription is unsubscribed, the user only needs to supply the
 * index (the reference number given by luaX_ref). The table containing
 * the subscription userdata and handler is removed from the subscription
 * table.
 *
 * The LCM userdata __gc does the same thing, but for all remaining entries.
 */

/* lcm userdata */
typedef struct impl_lcm_userdata {
	lcm_t * lcm;
	lua_State * handler_lua_State;
} impl_lcm_userdata_t;

/* methods */
static int impl_lcm_new(lua_State *); /* constructor */
static int impl_lcm_subscribe(lua_State *);
static int impl_lcm_unsubscribe(lua_State *);
static int impl_lcm_publish(lua_State *);
static int impl_lcm_handle(lua_State *);
static int impl_lcm_timedhandle(lua_State *);

/* metamethods */
static int impl_lcm_tostring(lua_State *);
static int impl_lcm_gc(lua_State *);

/* supporting functions */
static void impl_lcm_c_handler(const lcm_recv_buf_t *, const char *, void *);
static impl_lcm_userdata_t * impl_lcm_newuserdata(lua_State *);
static impl_lcm_userdata_t * impl_lcm_checkuserdata(lua_State *, int);
static void impl_lcm_createsubscriptiontable(lua_State *, int);
static void impl_lcm_getsubscriptiontable(lua_State *, int);
static void * impl_lcm_addtosubscriptiontable(lua_State *, int,
		int **, lcm_subscription_t ***);
static int impl_lcm_getfromsubscriptiontable(lua_State *, int,
		int, lcm_subscription_t **);
static int impl_lcm_removefromsubscriptiontable(lua_State *, int,
		int, lcm_subscription_t **);

/* subscription userdata */
typedef struct impl_sub_userdata {
	lcm_subscription_t * subscription;
	impl_lcm_userdata_t * owning_lcm_userdata;
	int ref_num;
} impl_sub_userdata_t;

static int impl_abs_index(lua_State * L, int i){
	return i > 0 ? i : i <= LUA_REGISTRYINDEX ? i : lua_gettop(L) + 1 + i;
}

/**
 * Makes LCM userdatas' metatable. The metatable is named "lcm.lcm".
 *
 * @post A metatable exists named "lcm.lcm" and contains all of the LCM
 *     userdata's member functions.
 *
 * @param L The Lua state.
 */
void ll_lcm_makemetatable(lua_State * L){

	/* create empty meta table */
	if(!luaL_newmetatable(L, "lcm.lcm")){
		lua_pushstring(L, "cannot create metatable");
		lua_error(L);
	}

	const struct luaL_Reg metas[] = {
		{"__tostring", impl_lcm_tostring},
		{"__gc", impl_lcm_gc},
		{NULL, NULL},
	};

	/* register to meta */
	luaX_registertable(L, metas);

	const struct luaL_Reg methods[] = {
		{"subscribe", impl_lcm_subscribe},
		{"unsubscribe", impl_lcm_unsubscribe},
		{"publish", impl_lcm_publish},
		{"handle", impl_lcm_handle},
		{"timedhandle", impl_lcm_timedhandle},
		{NULL, NULL},
	};

	/* register methods to new table, set __index */
	lua_pushstring(L, "__index");
	lua_newtable(L);
	luaX_registertable(L, methods);
	lua_rawset(L, -3);

	/* TODO hide metatable */
	/*lua_pushstring(L, "__metatable");
	lua_pushnil(L);
	lua_rawset(L, -3);*/

	/* pop the metatable */
	lua_pop(L, 1);
}

/**
 * Registers all LCM functions to the LCM module. At the moment, only one
 * function is registered: the LCM constructor.
 *
 * @post All LCM functions have been added to the LCM module.
 *     The modules table is on the top of the stack.
 *
 * @param L The Lua state.
 */
void ll_lcm_register_new(lua_State * L){

	const struct luaL_Reg new_function[] = {
		{"new", impl_lcm_new},
		{NULL, NULL},
	};

	luaX_registerglobal(L, "lcm.lcm", new_function);
}

/**
 * Creates and initializes an LCM userdata.
 *
 * Optionally takes one argument, a string, containing the LCM provider. If no
 * provider is supplied, the LCM userdata is created using the environment
 * variable LCM_DEFAULT_URL if it is defined, or the default
 * "udpm://239.255.76.67:7667".
 *
 * @see lcm_create
 *
 * @pre The Lua arguments on the stack:
 *     A string for the provider.
 *
 * @post The Lua return values on the stack:
 *     The new LCM userdata.
 *
 * @param L The Lua state.
 * @return The number of return values on the Lua stack.
 *
 * @throws Lua error if LCM userdata cannot be created.
 */
static int impl_lcm_new(lua_State * L){

	/* we expect 1 argument */
	lua_settop(L, 1);

	/* check for a string containing the provider */
	const char * provider = NULL;
	if(!lua_isnil(L, 1)){
		/* if there was no argument, the stack will have nil */
		provider = luaL_checkstring(L, 1);
	}

	/* open the lcm connection */
	lcm_t * lc = lcm_create(provider);

	/* check for failure */
	if(!lc){
		lua_pushstring(L, "error lcm create");
		lua_error(L);
	}

	/* create lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_newuserdata(L);
	lcmu->lcm = lc;

	/* create a subscription table */
	impl_lcm_createsubscriptiontable(L, -1);

	/* return lcm userdata, which is on top of the stack */

	return 1;
}

/**
 * Publishes a message.
 *
 * @see lcm_publish
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata (self), a string containing the channel, and a string
 *     containing message data (from an encode).
 *
 * @post The Lua return values on the stack:
 *     Nothing.
 *
 * @param L The Lua state.
 * @return The number of return values on the Lua stack.
 *
 * @throws Lua error if the message cannot be published.
 */
static int impl_lcm_publish(lua_State * L){

	/* we expect 3 arguments */
	lua_settop(L, 3);

	/* get the lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_checkuserdata(L, 1);

	/* get the channel */
	const char * channel = luaL_checkstring(L, 2);

	/* get the buffer */
	size_t data_size;
	const char * data = luaL_checklstring(L, 3, &data_size);

	/* publish the message */
	if(lcm_publish(lcmu->lcm, channel, data, data_size) != 0){
		lua_pushstring(L, "error lcm publish");
		lua_error(L);
	}

	return 0;
}

/**
 * Subscribes to a channel. Requires the user to specify a Lua handler (which
 * is called by generic_lcm_handler). The whole process is somewhat involved.
 *
 * In Lua 5.1, the handlers are stored in the LCM userdata's environment.
 *
 * Subscriptions are managed by the LCM userdata, and the user is not given
 * access to the subscription userdata. This subscribe method returns
 * subscription reference number, which is used to unsubscribe later. If the
 * user does not manually unsubscribe, the subscription will automatically
 * terminate during garbage collection of the LCM userdata.
 *
 * @see lcm_subscribe
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata (self), a string containing the channel, and a function
 *     for the handler.
 *
 * @post The Lua return values on the stack:
 *     A subscription reference number.
 *
 * @param L The Lua state.
 * @return The number of return values on the Lua stack.
 */
static int impl_lcm_subscribe(lua_State * L){
	
	/* we expect 3 arguments */
	lua_settop(L, 3);

	/* get the lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_checkuserdata(L, 1);

	/* get the channel */
	const char * channel = luaL_checkstring(L, 2);

	/* check the handler */
	luaL_checktype(L, 3, LUA_TFUNCTION);

	/* add a subscription table entry */
	int * ref_num_ptr;
	lcm_subscription_t ** subscription;
	void * userdata =
			impl_lcm_addtosubscriptiontable(L, 1, &ref_num_ptr, &subscription);

	/* pop subscription table */
	lua_pop(L, 1);

	/* do the actual subscribe */
	*subscription =
			lcm_subscribe(lcmu->lcm, channel, impl_lcm_c_handler, userdata);

	/* push reference number */
	lua_pushinteger(L, *ref_num_ptr);

	return 1;
}

/**
 * Handles an incoming message. Calls lcm_handle. Just like lcm_handle, handler
 * functions are invoked one at a time, in the order they were subscribed,
 * during the execution of this function.
 *
 * Notice that the handle method prepares the Lua stack for the handler
 * functions. When the handler functions execute, the Lua stack contains only
 * the LCM userdata.
 *
 * Recursive calls to handle are not allowed, therefore handlers must not
 * call handle.
 *
 * @see lcm_handle
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata (self).
 *
 * @post The Lua return values on the stack:
 *     Nothing.
 *
 * @param L The Lua state.
 * @return The number of return values on the Lua stack.
 *
 * @throws Lua error if the message cannot be handled.
 */
static int impl_lcm_handle(lua_State * L){
	
	/* we expect 1 argument */
	lua_settop(L, 1);

	/* get the lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_checkuserdata(L, 1);

	/* update the lua state */
	lcmu->handler_lua_State = L;

	/* call lcm handle */
	if(lcm_handle(lcmu->lcm) != 0){
		lua_pushstring(L, "error lcm handle");
		lua_error(L);
	}

	return 0;
}

/**
 * Handles an incoming message. Only blocks for the given amount of time. Calls
 * lcm_handle. Just like lcm_handle, handler functions are invoked one at a
 * time, in the order they were subscribed, during the execution of this
 * function.
 *
 * Notice that the handle method prepares the Lua stack for the handler
 * functions. When the handler functions execute, the Lua stack contains only
 * the LCM userdata.
 *
 * Recursive calls to handle are not allowed, therefore handlers must not
 * call handle.
 *
 * @see lcm_handle
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata (self), and an integer holding the amount of  time to
 *     block (in milliseconds).
 *
 * @post The Lua return values on the stack:
 *     A boolean: true if a message was handled, false otherwise.
 *
 * @param L The Lua state.
 * @return The number of return values on the Lua stack.
 *
 * @throws Lua error if the message cannot be handled.
 */
static int impl_lcm_timedhandle(lua_State * L){

	/* we expect 2 arguments */
	lua_settop(L, 2);

	/* get the lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_checkuserdata(L, 1);

	/* check for a integer timeout */
	int timeout_microsec = luaL_checkint(L, 2);

	/* update the lua state */
	lcmu->handler_lua_State = L;

	int fd = lcm_get_fileno(lcmu->lcm);
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	struct timeval timeout = { 0, timeout_microsec };

	int status = select(fd + 1, &fds, 0, 0, &timeout);

	if(status == 0){

		/* timeout, return false */
		lua_pushboolean(L, 0);

	}else if(FD_ISSET(fd, &fds)){

		/* read for handle */
		if(lcm_handle(lcmu->lcm) != 0){
			lua_pushstring(L, "error lcm handle");
			lua_error(L);
		}

		/* ok, return true */
		lua_pushboolean(L, 1);

	}else{

		/* select must have encountered an error */
		lua_pushstring(L, "error lcm handle (select)");
		lua_error(L);
	}

	return 1;
}

/**
 * Used by the LCM C API to dispatch a Lua handler. This function invokes a
 * Lua handlers, which is stored in the subscribtion userdata, which was
 * created by the subscribe method.
 *
 * In order to do its work, this function need a pointer to the Lua stack
 * used by the handle function, a lcm_subscription_t pointer, and a Lua
 * handler function.
 *
 * The handle method prepares the Lua stack for the handlers. The userdata
 * supplied to this function is always the subscription userdata created during
 * subscribe.
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata.
 *
 * @post The Lua return values on the stack:
 *     A LCM userdata.
 * 
 * @post On exit, the Lua stack is the same as it was on entrance.
 *
 * @param recv_buf The receive buffer containing the message.
 * @param channel The channel of the message.
 * @param userdata The userdata given during the call to handle. For this
 *     function the userdata is always the subscription userdata created
 *     during the call to subscribe.
 *
 * @throws The handler function may propagate Lua errors.
 */
static void impl_lcm_c_handler(const lcm_recv_buf_t * recv_buf,
		const char * channel, void * userdata){

	/* get the subscription userdata */
	impl_sub_userdata_t * subu =
			(impl_sub_userdata_t *) userdata;

	/* get the current lua state */
	lua_State * L = subu->owning_lcm_userdata->handler_lua_State;

	/* get the ref_num */
	int ref_num = subu->ref_num;

	/* remeber, lcm userdata is the only value on the stack */
	/* this was setup by handle function */

	/* get subscription table entry, this pushes the handler on the stack */
	lcm_subscription_t * subscription;
	if(!impl_lcm_getfromsubscriptiontable(L, 1, ref_num, &subscription)){
		/* this should never happen */
		lua_pushstring(L, "lcm handler cannot find lua handler");
		lua_error(L);
	}

	/* push channel */
	lua_pushstring(L, channel);

	/* push buffer as a binary string */
	lua_pushlstring(L, (const char *) recv_buf->data, recv_buf->data_size);

	/* call the handler */
	lua_call(L, 2, 0);
}

/**
 * Unsubscribes from a channel. Removes the subscription from the internal
 * subscription table.
 *
 * @see lcm_unsubscribe
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata (self), and a subscription reference number.
 *
 * @post The Lua return values on the stack:
 *     Nothing.
 *
 * @param L The Lua state.
 * @return The number of return values on the Lua stack.
 *
 * @throws Lua error if the subscription cannot be unsubscribed.
 */
static int impl_lcm_unsubscribe(lua_State * L){
	
	/* we expect 2 arguments */
	lua_settop(L, 2);

	/* get the lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_checkuserdata(L, 1);

	/* get the ref_num */
	int ref_num = luaL_checkint(L, 2);

	/* get subscription table entry, this pushes the handler on the stack */
	lcm_subscription_t * subscription;
	if(!impl_lcm_removefromsubscriptiontable(L, 1, ref_num, &subscription)){
		/* made up reference number */
		lua_pushstring(L, "subscription number invalid");
		lua_error(L);
	}

	/* unsubscribe */
	if(lcm_unsubscribe(lcmu->lcm, subscription) != 0){
		lua_pushstring(L, "error lcm unsubscribe");
		lua_error(L);
	}

	return 0;
}

/**
 * Creates a string from an LCM userdata. This is the __tostring metamethod of
 * the LCM userdata.
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata (self).
 *
 * @post The Lua return values on the stack:
 *     A string representing the LCM userdata.
 *
 * @param L The Lua state.
 * @return The number of return values on the Lua stack.
 */
static int impl_lcm_tostring(lua_State * L){

	/* we expect 1 argument */
	lua_settop(L, 1);

	/* get the lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_checkuserdata(L, 1);

	/* make the string */
	lua_pushfstring(L, "lcm.lcm [v%d.%d.%d] (@ %p)",
			LCM_MAJOR_VERSION, LCM_MINOR_VERSION, LCM_MICRO_VERSION, lcmu);

	return 1;
}

/**
 * Cleans up the LCM userdata. This is the __gc metamethod of the LCM userdata.
 * This method is called automatically by the Lua garbage collector.
 *
 * Automatically unsubscribes all channels.
 *
 * @see lcm_destroy
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata (self).
 *
 * @post The Lua return values on the stack:
 *     Nothing.
 *
 * @param L The Lua state.
 * @return The number of return values on the Lua stack.
 */
static int impl_lcm_gc(lua_State * L){
	
	/* we expect 1 argument */
	lua_settop(L, 1);

	/* get the lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_checkuserdata(L, 1);

	/* check if this userdata was ever created */
	if(!lcmu->lcm){
		return 0;
	}

	/* get subscription table */
	impl_lcm_getsubscriptiontable(L, 1);

	/* subscription table traversal */
	lua_pushnil(L); /* first key */
	while(lua_next(L, 2) != 0){

		/* get the subscription userdata */
		lua_pushstring(L, "userdata");
		lua_rawget(L, -2);

		impl_sub_userdata_t * subu =
				(impl_sub_userdata_t *) lua_touserdata(L, -1);

		/* unsubscribe */
		if(lcm_unsubscribe(lcmu->lcm, subu->subscription) != 0){
			lua_pushstring(L, "error lcm unsubscribe");
			lua_error(L);
		}

		/* pop the userdata and subscription table entry */
		lua_pop(L, 2);
	}

	/* free lcm */
	lcm_destroy(lcmu->lcm);

	/* clear out the subscription table */
	impl_lcm_createsubscriptiontable(L, 1);

	return 0;
}

/**
 * Creates a new LCM userdata. The userdata is defined using the "lcm.lcm"
 * metatable.
 *
 * @pre The Lua arguments on the stack:
 *     Nothing.
 *
 * @pre The metatable "lcm.lcm" is defined.
 *
 * @post The Lua return values on the stack:
 *     The new LCM userdata.
 *
 * @param L The Lua state.
 * @return A pointer to the new LCM userdata.
 */
static impl_lcm_userdata_t * impl_lcm_newuserdata(lua_State * L){

	/* make new user data */
	impl_lcm_userdata_t * lcmu =
	  (impl_lcm_userdata_t *)
	  lua_newuserdata(L, sizeof(impl_lcm_userdata_t));

	/* initialize struct */
	lcmu->lcm = NULL;
	lcmu->handler_lua_State = NULL;

	/* set the metatable */
	luaL_getmetatable(L, "lcm.lcm");
	if(lua_isnil(L, -1)){
		lua_pushstring(L, "cannot find metatable");
		lua_error(L);
	}
	lua_setmetatable(L, -2);

	return lcmu;
}

/**
 * Checks for a LCM userdata at the given index. Checks the userdata type using
 * the "lcm.lcm" metatable.
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata at the given index.
 *
 * @pre The metatable "lcm.lcm" is defined.
 *
 * @post The Lua return values on the stack:
 *     Nothing.
 *
 * @param L The Lua state.
 * @param index The index of the LCM userdata.
 * @return A pointer to the LCM userdata.
 */
static impl_lcm_userdata_t * impl_lcm_checkuserdata(lua_State * L, int index){
	return (impl_lcm_userdata_t *)
	  luaL_checkudata(L, index, "lcm.lcm");
}

/**
 * Creates a new subscription table. Creates a blank table and then adds it to
 * the userdata's environment/uservalue.
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata at the given index.
 *
 * @post The Lua return values on the stack:
 *     Nothing.
 * @post The LCM userdata has a subscription table.
 *
 * @param L The Lua state.
 * @param index The index of the LCM userdata.
 */
static void impl_lcm_createsubscriptiontable(lua_State * L, int index){

	index = impl_abs_index(L, index);

	/* check userdata type */
	/* ...unnecessary, assume lcm userdata at index */

	/* create a blank table */
	lua_newtable(L);

	/* add subscription table */
	lua_pushstring(L, "subscriptions");
	lua_newtable(L);
	lua_rawset(L, -3);

	/* add a table to hold references */
	lua_pushstring(L, "subreflist");
	lua_newtable(L);
	lua_rawset(L, -3);

	/* set as environment */
	luaX_setfenv(L, index);
}

/**
 * Pushes LCM userdata's subscription table onto the stack. The LCM userdata's
 * subscription table can then be modified.
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata at the given index.
 *
 * @post The Lua return values on the stack:
 *     The subscription table, and the subscription table reference list.
 *
 * @param L The Lua state.
 * @param index The index of the LCM userdata.
 */
static void impl_lcm_getsubscriptiontable(lua_State * L, int index){

	/* index = impl_abs_index(L, index); */

	/* check userdata type */
	/* ...unnecessary, assume lcm userdata at index */

	/* get the environment */
	luaX_getfenv(L, index);

	/* get the subscription table */
	lua_pushstring(L, "subscriptions");
	lua_rawget(L, -2);

	/* get subscription reference list */
	lua_pushstring(L, "subreflist");
	lua_rawget(L, -3);

	/* remove the environment */
	lua_remove(L, -3);
}

/**
 * Add a new subscription to a subscription table.
 *
 * Given a LCM userdata and a handler function on the stack, create a new
 * entry in the subscription table. The handler function is popped off the
 * stack. The subscription double pointer is set to point to the underlying
 * subscription pointer stored in the subscription table. The reference pointer
 * is set to point to the reference number (used to index the subscription
 * table) stored in the subscription table. Weird, I know.
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata at the given index, and a handler function.
 *
 * @post The Lua return values on the stack:
 *     Nothing.
 *
 * @param L The Lua state.
 * @param index The index of the subscription table.
 * @param ref_num_ptr Used to return a pointer to the reference number.
 * @param subscription_double_ptr Used to return a pointer to a subscription
 *     pointer. This needs to be set by the caller.
 */
static void * impl_lcm_addtosubscriptiontable(lua_State * L, int index,
		int ** ref_num_ptr, lcm_subscription_t *** subscription_double_ptr){

	/* leave nothing new on the stack */
	const int initial_top = lua_gettop(L);

	/* save handler index for later */
	int handler_index = impl_abs_index(L, -1);

	/* get the lcm userdata */
	impl_lcm_userdata_t * lcmu = impl_lcm_checkuserdata(L, index);

	/* get the subscription table */
	impl_lcm_getsubscriptiontable(L, index);
	int subscription_table_index = impl_abs_index(L, -2);
	int subscription_ref_list_index = impl_abs_index(L, -1);

	/* the new subscription table entry */
	lua_newtable(L);

	/* add userdata to table */
	lua_pushstring(L, "userdata");
	impl_sub_userdata_t * subu =
			(impl_sub_userdata_t *)
			lua_newuserdata(L, sizeof(impl_sub_userdata_t));
	lua_rawset(L, -3);

	/* set the owning lcm userdata */
	subu->owning_lcm_userdata = lcmu;

	/* we need to return a pointer to this subscription pointer */
	*subscription_double_ptr = &subu->subscription;

	/* we need to return a pointer to this int */
	*ref_num_ptr = &subu->ref_num;

	/* add handler function to table */
	lua_pushstring(L, "handler");
	lua_pushvalue(L, handler_index);
	lua_rawset(L, -3);

	/* add to subscription table */
	subu->ref_num = luaX_ref(L, subscription_table_index,
			subscription_ref_list_index);

	/* leave nothing new on the stack */
	lua_settop(L, initial_top);

	/* pop handler table */
	lua_pop(L, 1);

	return (void *) subu;
}

/**
 * Get a subscription from a subscription table.
 *
 * Given a LCM userdata and reference number, push the handler function
 * onto the stack and return a pointer to the underlying subscription.
 *
 * @see impl_addtosubscriptiontable
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata at the given index.
 *
 * @post The Lua return values on the stack:
 *     The handler function.
 *
 * @param L The Lua state.
 * @param index The index of the LCM userdata.
 * @param ref_num The reference number into the subscription table.
 * @param subscription The subscription pointer.
 * @return Non-zero on success.
 */
static int impl_lcm_getfromsubscriptiontable(lua_State * L, int index,
		int ref_num, lcm_subscription_t ** subscription){

	/* get lcm userdata */
	impl_lcm_getsubscriptiontable(L, index);

	/* remove the reference list, don't need it */
	lua_pop(L, 1);

	/* get the subscription table entry */
	lua_rawgeti(L, -1, ref_num);

	/* remove subscription table */
	lua_remove(L, -2);

	/* make sure this is a real entry */
	if(lua_isnil(L, -1)){
		return 0;
	}

	/* get the userdata */
	lua_pushstring(L, "userdata");
	lua_rawget(L, -2);

	impl_sub_userdata_t * subu =
			(impl_sub_userdata_t *) lua_touserdata(L, -1);

	/* set the subscription */
	*subscription = subu->subscription;

	/* pop the userdata */
	lua_pop(L, 1);

	/* get the handler */
	lua_pushstring(L, "handler");
	lua_rawget(L, -2);

	/* remove the subscription table entry */
	lua_remove(L, -2);

	return 1;
}

/**
 * Remove a subscription from a subscription table.
 *
 * Given a LCM userdata and reference number, remove a subscription.
 *
 * @see impl_addtosubscriptiontable
 *
 * @pre The Lua arguments on the stack:
 *     A LCM userdata at the given index.
 *
 * @post The Lua return values on the stack:
 *     Nothing.
 *
 * @param L The Lua state.
 * @param index The index of the LCM userdata.
 * @param ref_num The reference number into the subscription table.
 * @param subscription The subscription pointer.
 * @return Non-zero on success.
 *
 * Doesn't push handler function.
 */
static int impl_lcm_removefromsubscriptiontable(lua_State * L, int index,
		int ref_num, lcm_subscription_t ** subscription){

	/* leave nothing new on the stack */
	const int initial_top = lua_gettop(L);

	/* get lcm userdata */
	impl_lcm_getsubscriptiontable(L, index);
	int subscription_table_index = impl_abs_index(L, -2);
	int subscription_ref_list_index = impl_abs_index(L, -1);

	/* get the subscription table entry */
	lua_rawgeti(L, subscription_table_index, ref_num);

	/* make sure this is a real entry */
	if(lua_isnil(L, -1)){
		return 0;
	}

	/* get the userdata */
	lua_pushstring(L, "userdata");
	lua_rawget(L, -2);

	impl_sub_userdata_t * subu =
			(impl_sub_userdata_t *) lua_touserdata(L, -1);

	/* set the subscription */
	*subscription = subu->subscription;

	/* remove from subscription table */
	luaX_unref(L, subscription_table_index,
			subscription_ref_list_index, ref_num);

	/* leave nothing new on the stack */
	lua_settop(L, initial_top);

	return 1;
}

