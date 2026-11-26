#!/bin/sh
# link-local.sh <configuration> <package>...
#
# Replaces `xpm link @micro-os-plus/<package> --config <configuration>` in
# this repository (micro-os-plus-iii-smp2): links
# build/<configuration>/xpacks/@micro-os-plus/<package> to the package's own
# folder in this repository (aarch32/, aarch64/, common/, cortexm/,
# posix-arch/), with a relative path. `xpm link` would instead go through
# ~/.local/xPacks/@micro-os-plus/<package>/.link, i.e. whatever working copy
# was last registered with `xpm link` on this machine.
#
# Run from micro-os-plus-iii/tests (xpm actions run there).
set -eu
[ $# -ge 2 ] || { echo "usage: $0 <configuration> <package>..." >&2; exit 2; }
config=$1; shift
tests=$(pwd -P)
root=$(cd "$tests/../.." && pwd -P)
dest="$tests/build/$config/xpacks/@micro-os-plus"
mkdir -p "$dest"
for pkg in "$@"; do
  pkg=${pkg#@micro-os-plus/}
  src=""
  for d in "$root"/*/"$pkg"; do [ -f "$d/package.json" ] && src=$d; done
  [ -n "$src" ] || { echo "link-local: $pkg not found under $root/*/" >&2; exit 1; }
  # build/<config>/xpacks/@micro-os-plus -> up 5 = micro-os-plus-iii, up 6 = repository root
  rel="../../../../../../${src#"$root"/}"
  rm -rf "${dest:?}/$pkg"
  ln -s "$rel" "$dest/$pkg"
  [ -f "$dest/$pkg/package.json" ] || { echo "link-local: bad link $dest/$pkg -> $rel" >&2; exit 1; }
  echo "link-local: @micro-os-plus/$pkg -> $rel"
done
