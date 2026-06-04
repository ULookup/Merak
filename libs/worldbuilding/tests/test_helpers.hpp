#pragma once

#include <cstdlib>
#include <string>

namespace merak::worldbuilding::test {

inline std::string test_pg_conninfo() {
    const char* env = std::getenv("MERAK_TEST_PG");
    if (env && env[0] != '\0') {
        return env;
    }
    return "dbname=merak_test";
}

} // namespace merak::worldbuilding::test
