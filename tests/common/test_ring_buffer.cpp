#include "common/SpscRingBuffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <thread>
#include <vector>

TEST_CASE("ring buffer reads back what was written, across the wrap point")
{
    gv::SpscRingBuffer<int> ring(8);
    std::vector<int> out(8);

    const int first[] = {1, 2, 3, 4, 5, 6};
    REQUIRE(ring.Write(first, 6) == 6);
    REQUIRE(ring.Read(out.data(), 4) == 4);
    CHECK(out[0] == 1);
    CHECK(out[3] == 4);

    const int second[] = {7, 8, 9, 10, 11};
    REQUIRE(ring.Write(second, 5) == 5); // wraps
    REQUIRE(ring.Read(out.data(), 8) == 7);
    CHECK(std::vector<int>(out.begin(), out.begin() + 7) == std::vector<int>{5, 6, 7, 8, 9, 10, 11});
}

TEST_CASE("ring buffer drops what doesn't fit and reads nothing when empty")
{
    gv::SpscRingBuffer<int> ring(4);
    const int data[] = {1, 2, 3, 4, 5, 6};
    CHECK(ring.Write(data, 6) == 4);
    std::vector<int> out(6);
    CHECK(ring.Read(out.data(), 6) == 4);
    CHECK(ring.Read(out.data(), 6) == 0);
}

TEST_CASE("ring buffer keeps order with a concurrent producer and consumer")
{
    gv::SpscRingBuffer<int> ring(64);
    constexpr int kTotal = 200000;

    std::thread producer([&] {
        int next = 0;
        while (next < kTotal)
        {
            int chunk[7];
            const int n = std::min(7, kTotal - next);
            std::iota(chunk, chunk + n, next);
            next += static_cast<int>(ring.Write(chunk, static_cast<size_t>(n)));
        }
    });

    int expected = 0;
    bool inOrder = true;
    std::vector<int> out(16);
    while (expected < kTotal)
    {
        const size_t n = ring.Read(out.data(), out.size());
        for (size_t i = 0; i < n; ++i)
        {
            inOrder = inOrder && out[i] == expected;
            ++expected;
        }
    }
    producer.join();
    CHECK(inOrder);
}
