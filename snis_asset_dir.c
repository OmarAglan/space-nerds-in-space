/*
	Copyright (C) 2019 Stephen M. Cameron
	Author: Stephen M. Cameron

	This file is part of Spacenerds In Space.

	Spacenerds in Space is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Spacenerds in Space is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Spacenerds in Space; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <stdio.h>
#include <stdlib.h>

#include "snis_asset_dir.h"

static char *default_asset_dir = STRPREFIX(DESTDIR) STRPREFIX(PREFIX) "/" DEFAULT_ASSET_DIR;

char *override_asset_dir(void)
{
	char *d;
	char *xdg_data_home;
	char *home;
	static char answer[PATH_MAX];

	d = getenv("SNIS_ASSET_DIR");
	if (!d) {
		/* return default_asset_dir;  Not anymore. */
		xdg_data_home = getenv("XDG_DATA_HOME");
		if (!xdg_data_home) {
			home = getenv("HOME");
			if (!home)
				return default_asset_dir;
			else {
				snprintf(answer, sizeof(answer),
					"%s/.local/share/space-nerds-in-space/share/snis", home);
				return answer;
			}
		} else {
				snprintf(answer, sizeof(answer),
					"%s/space-nerds-in-space/share/snis", xdg_data_home);
				return answer;
		}
	}
	return d;
}

