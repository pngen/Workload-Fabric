#pragma once
#include <cstdio>
#include <string>

namespace tf {
inline int g_failures = 0;
inline int g_checks = 0;
inline int g_passes = 0;
inline const char* g_case = "";
}
#define CHECK(cond) do { ++tf::g_checks; if(!(cond)){ ++tf::g_failures; std::printf("  FAIL [%s] %s:%d: %s\n", tf::g_case, __FILE__, __LINE__, #cond); } else { ++tf::g_passes; } } while(0)
#define CASE(name) do { tf::g_case = name; } while(0)
#define CHECK_EQ(a,b) do { ++tf::g_checks; auto _a=(a); auto _b=(b); if(!(_a==_b)){ ++tf::g_failures; std::printf("  FAIL [%s] %s:%d: %s == %s\n", tf::g_case, __FILE__, __LINE__, #a, #b); } else { ++tf::g_passes; } } while(0)
#define TEST_MAIN_END() do { std::printf("%s: %d checks, %d pass, %d fail\n", tf::g_case, tf::g_checks, tf::g_passes, tf::g_failures); return tf::g_failures == 0 ? 0 : 1; } while(0)
