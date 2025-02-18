#!/bin/sh

# This script is meant to be invoked only by snis_client

if [ "$(id -u)" = "0" ]
then
	printf "You are root. Do not run this script as root.\n" 1>&2
	exit 1
fi

if [ "$SNIS_DB_DIR" = "" ]
then
	SNIS_DB_DIR="$XDG_DATA_HOME"
	if [ "$SNIS_DB_DIR" = "" ]
	then
		SNIS_DB_DIR="$HOME/.local/share/space-nerds-in-space"
		if [ ! -d "$SNIS_DB_DIR" ]
		then
			mkdir -p "$SNIS_DB_DIR"
		fi
	fi
fi
export SNIS_DB_DIR

# Hack this in for now.  Note: the Makefile edits the next two lines with sed
# so be careful if you change it that the Makefile still edits it correctly.
# Currently it matches the pattern: '^PREFIX=[.]$' and '^DESTDIR=$'
PREFIX=.
DESTDIR=

BINDIR=${DESTDIR}${PREFIX}/bin

if [ "$SNIS_ASSET_DIR" = "" ]
then
	if [ "$XDG_DATA_HOME" = "" ]
	then
		export SNIS_ASSET_DIR=$HOME/.local/share/space-nerds-in-space/share/snis
	else
		export SNIS_ASSET_DIR=$XDG_DATA_HOME/space-nerds-in-space/share/snis
	fi
	if [ ! -d "$SNIS_ASSET_DIR" ]
	then
		mkdir -p "$SNIS_ASSET_DIR"
	fi
fi

echo $SNIS_ASSET_DIR | grep '[/]share[/]snis$' > /dev/null 2>&1
if [ "$?" != "0" ]
then
	echo "Error: SNIS_ASSET_DIR must end with /share/snis" 1>&2
	echo "Current value of SNIS_ASSET_DIR is $SNIS_ASSET_DIR" 1>&2
	echo "Exiting." 1>&2
	exit 1
fi

SNIS_ASSET_DIR_ROOT=$(echo $SNIS_ASSET_DIR | sed -e 's/[/]share[/]snis$//')

DESTDIR=
PREFIX=.

SNIS_UPDATE_ASSETS=${BINDIR}/snis_update_assets

# copy local assets first
echo "Updating local assets..."
echo "Changing directory to ${DESTDIR}${PREFIX}"

cd ${DESTDIR}${PREFIX}
if [ "$?" != 0 ]
then
	echo "Failed to cd to ${DESTDIR}${PREFIX}" 1>&2
	exit 1
fi

${SNIS_UPDATE_ASSETS} --force --destdir "$SNIS_ASSET_DIR_ROOT" --srcdir ./share/snis ;
# download assets from spacenerdsinspace.com
echo "Downloading remote assets from asset server..."
${SNIS_UPDATE_ASSETS} --force --destdir "$SNIS_ASSET_DIR_ROOT" ;

printf "\n\n\n--- Press Return ---\n\n\n"

read -r x


