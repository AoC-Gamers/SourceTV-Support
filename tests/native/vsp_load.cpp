// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <dlfcn.h>

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;

	// Load before any SDK libraries are loaded by the test process.
	void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!library) {
		std::fprintf(stderr, "%s\n", dlerror());
		return 1;
	}
	dlclose(library);
	std::puts("vsp_load: standalone dynamic load passed");
}
