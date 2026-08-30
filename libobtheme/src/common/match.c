/* Vendored from labwc 5aaa5da0571b src/common/match.c; local changes: see libobtheme/VENDORED-DIFFS.md */
// SPDX-License-Identifier: GPL-2.0-only

#include "common/match.h"
#include <fnmatch.h>

bool
match_glob(const char *pattern, const char *string)
{
	return fnmatch(pattern, string, FNM_CASEFOLD) == 0;
}
