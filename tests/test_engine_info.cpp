#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "engine/EngineInfo.h"

TEST_CASE ("engine reports a version and the slot count", "[engine][smoke]")
{
    REQUIRE (mosaic::engine::kNumSlots == 8);
    REQUIRE (std::strlen (mosaic::engine::versionString()) > 0);
}
