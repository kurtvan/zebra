#include "common/FitRect.hpp"

#include <catch2/catch_test_macros.hpp>

using gv::FitRect;
using gv::Rect;

TEST_CASE("FitRect fills a target of the same shape")
{
    CHECK(FitRect(1920, 1080, 1920, 1080) == Rect{0, 0, 1920, 1080});
    CHECK(FitRect(1920, 1080, 3840, 2160) == Rect{0, 0, 3840, 2160});
    CHECK(FitRect(3840, 2160, 1280, 720) == Rect{0, 0, 1280, 720});
}

TEST_CASE("FitRect letterboxes a wider canvas")
{
    // 32:9 on a 16:10 laptop screen: full width, bars top and bottom.
    CHECK(FitRect(3840, 1080, 3456, 2234) == Rect{0, 631, 3456, 972});
    CHECK(FitRect(1920, 1080, 1920, 1200) == Rect{0, 60, 1920, 1080});
}

TEST_CASE("FitRect pillarboxes a taller canvas")
{
    CHECK(FitRect(1080, 1920, 1920, 1080) == Rect{656, 0, 607, 1080});
    CHECK(FitRect(1440, 1080, 1920, 1080) == Rect{240, 0, 1440, 1080});
}

TEST_CASE("FitRect handles empty and tiny sizes")
{
    CHECK(FitRect(0, 1080, 1920, 1080) == Rect{});
    CHECK(FitRect(1920, 1080, 0, 0) == Rect{});
    CHECK(FitRect(7680, 320, 100, 100).height == 4);
    CHECK(FitRect(7680, 2, 100, 100).height == 1); // never collapses to nothing
}
