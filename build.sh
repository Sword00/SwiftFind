#!/usr/bin/env bash
# Build SwiftFind. Requires a MinGW-w64 g++ (w64devkit is the easy way).
#
#   ./build.sh                                   # g++ 已在 PATH 上
#   W64DEVKIT=/path/to/w64devkit ./build.sh      # 指定 w64devkit 根目录
set -e

if [ -n "$W64DEVKIT" ]; then
    export PATH="$W64DEVKIT/bin:$PATH"
fi

cd "$(dirname "$0")/src"

command -v g++ >/dev/null || {
    echo "找不到 g++。"
    echo "安装 w64devkit，把它的 bin 加进 PATH；或运行 W64DEVKIT=/path/to/w64devkit ./build.sh"
    exit 1
}

windres -I. res.rc -O coff -o res.o

# -Os beats -O2 by ~25 KB here; -ffunction-sections/--gc-sections then drop
# another ~4 KB of unreferenced libstdc++ members.
# Deliberately NOT enabled: -fno-exceptions (would strip .pdata/.xdata for our
# frames, turning an allocation failure into a crash instead of a clean exit).
g++ -Os -s -municode -mwindows -std=c++17 -DNDEBUG \
    -ffunction-sections -fdata-sections \
    main.cpp res.o -o SwiftFind.exe \
    -Wl,--gc-sections \
    -static -static-libgcc -static-libstdc++ \
    -lcomctl32 -lshell32 -lole32 -ldwmapi -lshlwapi -luxtheme -luuid \
    -lgdiplus \
    -luser32 -lgdi32 -ladvapi32

ls -l SwiftFind.exe | awk '{print "built src/SwiftFind.exe  ("$5" bytes)"}'
