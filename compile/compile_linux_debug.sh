# Debug build with AddressSanitizer + UndefinedBehaviorSanitizer. Run from compile/, like compile_linux_x11.sh.
# Same sources and warnings, but unoptimised with debug info and sanitizers; writes out/TheBenchProject_debug.
# Run it with run_linux_x11_debug.sh: memory errors (use-after-free, overflows, double frees, leaks) and
# undefined behaviour (bad casts, overflows, misaligned access) stop the game with a stack trace.
gcc ../source/*.c -I"../include" -std=c2x -Werror -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o "out/TheBenchProject_debug" -L"../lib" -l:libraylib.a -l:libwr.a -lGL -lm -lpthread -ldl -lrt -lX11
