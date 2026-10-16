# Builds and runs the headless test suite: tests/*.c plus every game source except main.c.
# Usage (from any directory): sh tests/run_linux.sh [--sanitize]
#   --sanitize  build with AddressSanitizer + UndefinedBehaviorSanitizer: slower, but memory errors and
#               undefined behaviour stop the run with a stack trace.
# The binary and the tests' scratch files go to tests/out/ (gitignored); tests/out/work is wiped first.
# Exit code: 0 when every test passed.
set -e

TestsDirectory=$(cd "$(dirname "$0")" && pwd)
RootDirectory=$(dirname "$TestsDirectory")
OutDirectory="$TestsDirectory/out"

Flags="-O1"
if [ "$1" = "--sanitize" ]; then
    Flags="-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
fi

mkdir -p "$OutDirectory"
GameSources=$(ls "$RootDirectory"/source/*.c | grep -v '/main\.c$')
gcc $GameSources "$TestsDirectory"/*.c -I"$RootDirectory/include" -I"$TestsDirectory" -std=c2x -Werror -Wall -Wextra -Wpedantic -Wconversion -Wshadow $Flags -o "$OutDirectory/TheBenchProjectTests" -L"$RootDirectory/lib" -l:libraylib.a -l:libwr.a -lGL -lm -lpthread -ldl -lrt -lX11

rm -rf "$OutDirectory/work"
cd "$OutDirectory"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./TheBenchProjectTests
