#include <gtest/gtest.h>
#include <benchmark/benchmark.h>

// 使用 gtest 初始化 benchmark，避免用 BENCHMARK_MAIN() 这个宏(宏也是调用的这个函数)
// BENCHMARK_MAIN() 会导致与 gtest 中的 main 函数冲突
TEST(benchmark, running) { ::benchmark::RunSpecifiedBenchmarks(); }
