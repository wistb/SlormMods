#!/usr/bin/env bash
# The build lives at the top of the repo; this runs it for this mod.
exec "$(dirname "$0")/../../build.sh" MapReveal "$@"
