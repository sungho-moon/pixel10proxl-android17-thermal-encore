#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to an extracted Android NDK}"
cc="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/${NDK_HOST_TAG:-linux-x86_64}/bin/aarch64-linux-android35-clang"
out=${1:-"$src/../../src/encore-fas/module/thread/bin/guardian"}
"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -Wl,-z,max-page-size=16384 "$src/guardian.c" -o "$out"
