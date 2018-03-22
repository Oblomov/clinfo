#!/bin/sh

# Change the version recorded in src/clinfo.c and man1/clinfo.1 to
# the current highest OpenCL supported standard followed by current
# yy.mm.dd

abort() {
	echo "$1" >&2
	exit 1
}

test -n "$(git status --porcelain | grep -v '??')" && abort "Uncommited changes, aborting"

DATE=$(date +%Y-%m-%d)
MAJOR=$(awk '/^OpenCL/ { print $NF ; exit }' man1/clinfo.1)
SUBV=$(date +%y.%m.%d)
VERSION="$MAJOR$SUBV"

sed -i -e "/clinfo version/ s/version \S\+\"/version $VERSION\"/" src/clinfo.c &&
sed -i -e "1 s/\".\+$/\"$DATE\" \"clinfo $VERSION\"/" man1/clinfo.1 &&
sed -i -e "1 s/\".\+$/version: $VERSION-{build}/" .appveyor.yml &&
git commit -m "Version $VERSION" -e -a &&
git tag -m "Version $VERSION" $VERSION
