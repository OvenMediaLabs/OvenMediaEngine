#include <catch2/catch.hpp>
#include <tl/expected.hpp>

#include <string>
#include <memory>
#include <system_error>

using std::string;

tl::expected<int, string> getInt3(int val) { return val; }

tl::expected<int, string> getInt2(int val) { return val; }

tl::expected<int, string> getInt1() { return getInt2(5).and_then(getInt3); }

TEST_CASE("Issue 1", "[issues.1]") { (void)getInt1(); }

tl::expected<int, int> operation1() { return 42; }

tl::expected<std::string, int> operation2(int const val) { (void)val; return "Bananas"; }

TEST_CASE("Issue 17", "[issues.17]") {
  auto const intermediate_result = operation1();

  (void)intermediate_result.and_then(operation2);
}

struct a {};
struct b : a {};

auto doit() -> tl::expected<std::unique_ptr<b>, int> {
    return tl::make_unexpected(0);
}

TEST_CASE("Issue 23", "[issues.23]") {
    tl::expected<std::unique_ptr<a>, int> msg = doit();
    REQUIRE(!msg.has_value());    
}

TEST_CASE("Issue 26", "[issues.26]") {
  tl::expected<a, int> exp = tl::expected<b, int>(tl::unexpect, 0);
  REQUIRE(!exp.has_value());
}

struct foo {
  foo() = default;
  foo(foo &) = delete;
  foo(foo &&){};
};

TEST_CASE("Issue 29", "[issues.29]") {
  std::vector<foo> v;
  v.emplace_back();
  tl::expected<std::vector<foo>, int> ov = std::move(v);
  REQUIRE(ov->size() == 1);
}

tl::expected<int, std::string> error() {
  return tl::make_unexpected(std::string("error1 "));
}
std::string maperror(std::string s) { return s + "maperror "; }

TEST_CASE("Issue 30", "[issues.30]") {
  (void)error().map_error(maperror);
}

struct i31{
  int i;
};
TEST_CASE("Issue 31", "[issues.31]") {
    const tl::expected<i31, int> a = i31{42};
    (void)a->i;

    tl::expected< void, std::string > result;
    tl::expected< void, std::string > result2 = result;
    result2 = result;
}

TEST_CASE("Issue 33", "[issues.33]") {
    tl::expected<void, int> res {tl::unexpect, 0};
    REQUIRE(!res);    
    res = res.map_error([](int i) { (void)i; return 42; });
    REQUIRE(res.error() == 42);
}


tl::expected<void, std::string> voidWork() { return {}; }
tl::expected<int, std::string> work2() { return 42; }
void errorhandling(std::string){}

TEST_CASE("Issue 34", "[issues.34]") {
  tl::expected <int, std::string> result = voidWork ()
      .and_then (work2);
  (void)result.map_error ([&] (std::string result) {errorhandling (result);});
}

struct non_copyable {
	non_copyable(non_copyable&&) = default;
	non_copyable(non_copyable const&) = delete;
	non_copyable() = default;
};

TEST_CASE("Issue 42", "[issues.42]") {
 	(void)tl::expected<non_copyable,int>{}.map([](non_copyable) {});
}

TEST_CASE("Issue 43", "[issues.43]") {
	auto result = tl::expected<void, std::string>{};
	result = tl::make_unexpected(std::string{ "foo" });
}

#if !(__GNUC__ <= 5)
#include <memory>

using MaybeDataPtr = tl::expected<int, std::unique_ptr<int>>;

MaybeDataPtr test(int i) noexcept
{
  return std::move(i);
}

MaybeDataPtr test2(int i) noexcept
{
  return std::move(i);
}

TEST_CASE("Issue 49", "[issues.49]") {
  auto m = test(10)
    .and_then(test2);
}
#endif

tl::expected<int, std::unique_ptr<std::string>> func()
{
  return 1;
}

TEST_CASE("Issue 61", "[issues.61]") {
  REQUIRE(func().value() == 1);
}

struct move_tracker {
        int moved = 0;

        move_tracker() = default;

        move_tracker(move_tracker const &other) noexcept;
        move_tracker(move_tracker &&orig) noexcept
            : moved(orig.moved + 1) {}

        move_tracker &
        operator=(move_tracker const &other) noexcept;
        
        move_tracker &operator=(move_tracker &&orig) noexcept {
          moved = orig.moved + 1;
          return *this;
        }
};

TEST_CASE("Issue 122", "[issues.122]") { 
     tl::expected<move_tracker, int> res;
     res.emplace();
     REQUIRE(res.value().moved == 0);
}

#ifdef __cpp_deduction_guides
TEST_CASE("Issue 89", "[issues.89]") { 
    auto s = tl::unexpected("Some string");
    REQUIRE(s.value() == std::string("Some string"));
}
#endif

struct S {
    int i = 0;
    int j = 0;
    S(int i) : i(i) {}
    S(int i, int j) : i(i), j(j) {}
};

TEST_CASE("Issue 107", "[issues.107]") {
    tl::expected<int, S> ex1(tl::unexpect, 2); 
    tl::expected<int, S> ex2(tl::unexpect, 2, 2);

    REQUIRE(ex1.error().i == 2);
    REQUIRE(ex1.error().j == 0);
    REQUIRE(ex2.error().i == 2);
    REQUIRE(ex2.error().j == 2);
}

TEST_CASE("Issue 129", "[issues.129]") {
  tl::expected<std::unique_ptr<int>, int> x1 {std::unique_ptr<int>(new int(4))};
  tl::expected<std::unique_ptr<int>, int> y1 {std::unique_ptr<int>(new int(2))};
  x1 = std::move(y1);

  REQUIRE(**x1 == 2);
}

TEST_CASE("PR 156", "[pr.156]") {
  tl::expected<void, int> a (tl::unexpect, 12);
  tl::expected<void, int> b (tl::unexpect, 12);
  tl::expected<void, int> c (tl::unexpect, 42);
  REQUIRE(!(a != b));
  REQUIRE(a != c);
  REQUIRE(b != c);
}

class MoveOnly {
public:
  MoveOnly() = default;

  // Non-copyable
  MoveOnly(const MoveOnly &) = delete;
  MoveOnly &operator=(const MoveOnly &) = delete;

  // Movable trivially
  MoveOnly(MoveOnly &&) = default;
  MoveOnly &operator=(MoveOnly &&) = default;
};

TEST_CASE("Issue 145", "[issues.145]") {
  tl::expected<MoveOnly, std::error_code> a{};
  tl::expected<MoveOnly, std::error_code> b = std::move(a);
  a = std::move(b);
}
