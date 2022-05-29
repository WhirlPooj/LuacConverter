#include "LuaCConversion.h"

int main() {

	lua_State* L = lua_open();
	luaL_openlibs(L);

	std::string in;
	while (true) {
		std::getline(std::cin, in);
		std::cout << lua2luac(L, in.c_str());
	}

	return 0;
}