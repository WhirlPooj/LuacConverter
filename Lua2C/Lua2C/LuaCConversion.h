#include <Windows.h>
#include <cstdint>

#include <iostream>

#include <sstream>
#include <string>

#include <vector>

extern "C" {
#include "lua/lua.h"
#include "lua/ldo.h"
#include "lua/lvm.h"
#include "lua/lualib.h"
#include "lua/lstate.h"
#include "lua/lstring.h"
#include "lua/lauxlib.h"
#include "lua/luaconf.h"
#include "lua/llimits.h"
#include "lua/lapi.h"
#include "lua/lfunc.h"
#include "lua/lopcodes.h"
#include "lua/lobject.h"
#include "lua/lundump.h"
}

/* written by icedmilk */
/* only some stuff works, excluding closures and loops */
/* i am 6 years too late */

/* luav exec raw stuff */

#define runtime_check(L, c)	{ if (!(c)) break; }

#define RA(i)	(base+GETARG_A(i))
/* to be used after possible stack reallocation */
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))
#define KBx(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, k+GETARG_Bx(i))


#define dojump(L,pc,i)	{(pc) += (i); luai_threadyield(L);}


#define Protect(x)	{ L->savedpc = pc; {x;}; base = L->base; }

void luaTv2luaC(lua_State* L, TValue* v, TValue* c) {
	switch (v->tt) {
	case LUA_TNIL: {
		c->tt = LUA_TNIL;
		break;
	}
	case LUA_TBOOLEAN:
	{
		c->tt = LUA_TBOOLEAN;
		c->value.b = v->value.b;
		break;
	}
	case LUA_TSTRING: {
		TString* s = reinterpret_cast<TString*>(v->value.gc);
		const char* str = (const char*)(s + 1);
		c->tt = LUA_TSTRING;
		c->value.gc = reinterpret_cast<GCObject*>(luaS_newlstr(L, str, s->tsv.len));
		break;
	}
	case LUA_TFUNCTION: {
		c->tt = LUA_TFUNCTION;
		c->value.p = v->value.p;
	}
	case LUA_TNUMBER: {
		double n = v->value.n;
		c->tt = LUA_TNUMBER;
		c->value.n = v->value.n;
	}
	}
}

std::vector<std::string> luaCExe(lua_State* L) {

	LClosure* cl;
	TValue* k;
	StkId base;

	const Instruction* pc;

	lua_assert(isLua(L->ci));
	cl = &clvalue(L->ci->func)->l;
	base = L->base;
	pc = L->savedpc;
	k = cl->p->k;

	std::vector<std::string> out;

	TValue* c = (TValue*)luaM_realloc_(L, 0, 0, sizeof(TValue) * cl->p->sizek);

	for (int i = 0; i < cl->p->sizek; i++) {
		luaTv2luaC(L, &k[i], (&c[i]));
	}

	for (;;) {
		const Instruction i = *pc++;
		StkId ra = RA(i);

		int op = GET_OPCODE(i);
		switch (op) {
		case OP_MOVE: {
			out.push_back("pushvalue -1");
			continue;
		}
		case OP_LOADK: {
			TValue* uv = &c[GETARG_Bx(i)];
			switch (uv->tt) {
			case LUA_TSTRING: {
				GCObject* gbl = uv->value.gc;
				TString* s = (TString*)gbl;
				const char* str = (const char*)(s + 1);
				out.push_back("pushstring " + std::string(str));
				break;
			}
			case LUA_TNUMBER: {
				int n = uv->value.n;
				out.push_back("pushnumber " + std::to_string(n));
				break;
			}
			default: {
				printf("other_type:%s\n", lua_typename(L, uv->tt));
				break;
			}
			}

			continue;
		}
		case OP_LOADBOOL: {
			int b = GETARG_B(i);
			out.push_back("pushboolean " + std::to_string(b));
			continue;
		}
		case OP_GETGLOBAL: {
			GCObject* gbl = KBx(i)->value.gc;
			TString* s = (TString*)gbl;
			const char* str = (const char*)(s + 1);
			out.push_back("getglobal " + std::string(str));
			continue;
		}
		case OP_GETTABLE: {
			StkId t = RKC(i);

			GCObject* gbl = t->value.gc;
			TString* s = (TString*)gbl;
			const char* str = (const char*)(s + 1);
			out.push_back("getfield -1 " + std::string(str));

			continue;
		}
		case OP_SETGLOBAL: {
			GCObject* gbl = KBx(i)->value.gc;
			TString* s = (TString*)gbl;
			const char* str = (const char*)(s + 1);
			out.push_back("setglobal " + std::string(str));

			continue;
		}
		case OP_SETTABLE: {
			StkId t = RKB(i);

			GCObject* gbl = t->value.gc;
			TString* s = (TString*)gbl;
			const char* str = (const char*)(s + 1);
			out.push_back("setfield -1 " + std::string(str));

			continue;
		}
		case OP_SELF: {
			StkId t = RKC(i);

			GCObject* gbl = t->value.gc;
			TString* s = (TString*)gbl;
			const char* str = (const char*)(s + 1);
			out.push_back("getfield -1 " + std::string(str));

			continue;
		}
		case OP_CALL: {
			int b = GETARG_B(i);
			int nresults = GETARG_C(i) - 1;
			if (b != 0) L->top = ra + b;
			L->savedpc = pc;
			if (b - 1 > 0)
				out.push_back("pcall " + std::to_string(b - 1) + " " + std::to_string(nresults) + " 0 ");
			else
				out.push_back("pcall " + std::to_string(b) + " " + std::to_string(nresults) + " 0 ");
			continue;
		}
		case OP_RETURN: {
			out.push_back("emptystack");
			return out;
		}
		default: {
			printf("other opcode: %s\n", luaP_opnames[op]);
			break;
		}
		}
	}

	/* end */
	out.push_back("emptystack");
	return out;
}


std::string lua2luac(lua_State* L, const char* str) {
	std::vector<std::string> c;
	std::string fin;

	if (luaL_loadbuffer(L, str, strlen(str), "Lua2C"))
		lua_pop(L, 1);
	else {

		if (luaD_precall(L, L->top - 1, 0) == 0)
			c = luaCExe(L);
		for (std::string s : c)
			fin.append(s + "\n");
	}

	return fin;
}