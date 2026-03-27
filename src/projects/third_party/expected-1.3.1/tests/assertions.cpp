#include <catch2/catch.hpp>
#include <stdexcept>

#define TL_ASSERT(cond) if (!(cond)) { throw std::runtime_error(std::string("assertion failure")); }

#include <tl/expected.hpp>

TEST_CASE("Assertions", "[assertions]") {
  tl::expected<int,int> o1 = 42;
  REQUIRE_THROWS_WITH(o1.error(), "assertion failure");

  tl::expected<int,int> o2 {tl::unexpect, 0};
  REQUIRE_THROWS_WITH(*o2, "assertion failure");

  struct foo { int bar; };
  tl::expected<struct foo,int> o3 {tl::unexpect, 0};
  REQUIRE_THROWS_WITH(o3->bar, "assertion failure");
}
