# Runs the sanitizer build from compile_linux_debug.sh, from any directory, like run_linux_x11.sh; its arguments go to the game.
# UBSan and ASan errors stop the game at the first problem with a stack trace. Leaks are reported at exit;
# lsan_suppressions.txt hides leaks inside the GL/X11 system libraries, which the game cannot fix.
cd "$(dirname "$0")/out"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 LSAN_OPTIONS=suppressions=../lsan_suppressions.txt:print_suppressions=0 ./TheBenchProject_debug "$@"
