// 版本号只改这一个文件：main.cpp 用字符串，res.rc 用数字，两边不会再对不上。
// 发版时改这四行，然后 ./build.sh。
#define SF_VER_MAJOR 1
#define SF_VER_MINOR 0
#define SF_VER_PATCH 3

#define SF_VER_STR    "1.0.3"      // res.rc 用
#define SF_VER_STR4   "1.0.3.0"    // res.rc 的四段式
#define SF_VER_STRW   L"1.0.3"     // main.cpp 用
