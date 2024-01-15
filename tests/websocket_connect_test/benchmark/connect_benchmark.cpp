#include <benchmark/benchmark.h>
#include <array>

constexpr int len = 6;
constexpr auto my_pow(const int i)
{
    return i * i;
}

static void bench_array_operator(benchmark::State& state)
{
    std::array<int, len> arr;
    constexpr int i = 1;
    for (auto _: state) {
        arr[0] = my_pow(i);
        arr[1] = my_pow(i+1);
        arr[2] = my_pow(i+2);
        arr[3] = my_pow(i+3);
        arr[4] = my_pow(i+4);
        arr[5] = my_pow(i+5);
    }
}
BENCHMARK(bench_array_operator);
