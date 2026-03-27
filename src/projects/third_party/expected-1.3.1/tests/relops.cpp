#include <catch2/catch.hpp>
#include <tl/expected.hpp>

TEST_CASE("Relational operators", "[relops]") {
  tl::expected<int, int> o1 = 42;
  tl::expected<int, int> o2{tl::unexpect, 0};
  const tl::expected<int, int> o3 = 42;

  REQUIRE(o1 == o1);
  REQUIRE(o1 != o2);
  REQUIRE(o1 == o3);
  REQUIRE(o3 == o3);

  tl::expected<void, int> o6;

  REQUIRE(o6 == o6);
}
