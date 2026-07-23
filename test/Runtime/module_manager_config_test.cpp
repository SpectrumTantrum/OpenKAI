#include "../../src/Module/ModuleMgr.h"

#include <iostream>

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::cerr << "usage: module_manager_config_test <config>" << std::endl;
		return 2;
	}

	kai::ModuleMgr modules;
	if (!modules.parseJsonFile(argv[1]))
	{
		std::cerr << "config parse failed" << std::endl;
		return 1;
	}
	if (!modules.createAll())
	{
		std::cerr << "module creation failed" << std::endl;
		return 1;
	}

	const bool valid =
		modules.findModule("disabledBoolean") == nullptr &&
		modules.findModule("disabledInteger") == nullptr &&
		modules.findModule("enabledBoolean") != nullptr &&
		modules.findModule("enabledInteger") != nullptr &&
		modules.findModule("enabledDefault") != nullptr;
	if (!valid)
	{
		std::cerr << "boolean/integer bON semantics are incorrect" << std::endl;
		return 1;
	}

	std::cout << "ModuleMgr bON config tests passed" << std::endl;
	return 0;
}
