#include <stdio.h>

#include <clang/Basic/Version.h>

#include "gitversion.h"
#include "version.h"

void pet_print_version(void)
{
	printf("%s\n", clang::getClangFullVersion().c_str());
	printf("%s\n", GIT_HEAD_ID);
}
