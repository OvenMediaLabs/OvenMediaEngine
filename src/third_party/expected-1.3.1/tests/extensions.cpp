#include <catch2/catch.hpp>
#include <tl/expected.hpp>

#define TOKENPASTE(x, y) x##y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#undef STATIC_REQUIRE
#define STATIC_REQUIRE(e)                                                      \
  constexpr bool TOKENPASTE2(rqure, __LINE__) = e;                             \
  (void)TOKENPASTE2(rqure, __LINE__);                                          \
  REQUIRE(e);

TEST_CASE("Map extensions", "[extensions.map]") {
  auto mul2 = [](int a) { return a * 2; };
  auto ret_void = [](int a) { (void)a; };

  {
    tl::expected<int, int> e = 21;
    auto ret = e.map(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.map(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).map(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).map(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.map(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.map(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).map(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).map(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = e.map(ret_void);
    REQUIRE(ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.map(ret_void);
    REQUIRE(ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).map(ret_void);
    REQUIRE(ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).map(ret_void);
    REQUIRE(ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.map(ret_void);
    REQUIRE(!ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.map(ret_void);
    REQUIRE(!ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).map(ret_void);
    REQUIRE(!ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).map(ret_void);
    REQUIRE(!ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }


  // mapping functions which return references
  {
    tl::expected<int, int> e(42);
    auto ret = e.map([](int& i) -> int& { return i; });
    REQUIRE(ret);
    REQUIRE(ret == 42);
  }
}

TEST_CASE("Map error extensions", "[extensions.map_error]") {
  auto mul2 = [](int a) { return a * 2; };
  auto ret_void = [](int a) { (void)a; };

  {
    tl::expected<int, int> e = 21;
    auto ret = e.map_error(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.map_error(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).map_error(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).map_error(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.map_error(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 42);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.map_error(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 42);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).map_error(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 42);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).map_error(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 42);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = e.map_error(ret_void);
    REQUIRE(ret);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.map_error(ret_void);
    REQUIRE(ret);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).map_error(ret_void);
    REQUIRE(ret);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).map_error(ret_void);
    REQUIRE(ret);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.map_error(ret_void);
    REQUIRE(!ret);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.map_error(ret_void);
    REQUIRE(!ret);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).map_error(ret_void);
    REQUIRE(!ret);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).map_error(ret_void);
    REQUIRE(!ret);
  }

}

TEST_CASE("And then extensions", "[extensions.and_then]") {
  auto succeed = [](int a) { (void)a; return tl::expected<int, int>(21 * 2); };
  auto fail = [](int a) { (void)a; return tl::expected<int, int>(tl::unexpect, 17); };

  {
    tl::expected<int, int> e = 21;
    auto ret = e.and_then(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.and_then(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).and_then(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).and_then(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = e.and_then(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 17);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.and_then(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 17);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).and_then(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 17);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).and_then(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 17);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.and_then(succeed);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.and_then(succeed);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).and_then(succeed);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).and_then(succeed);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.and_then(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.and_then(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).and_then(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).and_then(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }
}

TEST_CASE("or_else", "[extensions.or_else]") {
  using eptr = std::unique_ptr<int>;
  auto succeed = [](int a) { (void)a; return tl::expected<int, int>(21 * 2); };
  auto succeedptr = [](eptr e) { (void)e; return tl::expected<int,eptr>(21*2);};
  auto fail =    [](int a) { (void)a; return tl::expected<int,int>(tl::unexpect, 17);};
  auto failptr = [](eptr e) { *e = 17;return tl::expected<int,eptr>(tl::unexpect, std::move(e));};
  auto failvoid = [](int) {};
  auto failvoidptr = [](const eptr&) { /* don't consume */};
  auto consumeptr = [](eptr) {};
  auto make_u_int = [](int n) { return std::unique_ptr<int>(new int(n));};

  {
    tl::expected<int, int> e = 21;
    auto ret = e.or_else(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.or_else(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).or_else(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, eptr> e = 21;
    auto ret = std::move(e).or_else(succeedptr);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).or_else(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = e.or_else(fail);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.or_else(fail);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).or_else(fail);
    REQUIRE(ret);
    REQUIRE(ret == 21);
  }


  {
    tl::expected<int, eptr> e = 21;
    auto ret = std::move(e).or_else(failptr);
    REQUIRE(ret);
    REQUIRE(ret == 21);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).or_else(fail);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.or_else(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.or_else(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).or_else(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, eptr> e(tl::unexpect, make_u_int(21));
    auto ret = std::move(e).or_else(succeedptr);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).or_else(succeed);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.or_else(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 17);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.or_else(failvoid);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.or_else(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 17);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.or_else(failvoid);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).or_else(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 17);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).or_else(failvoid);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, eptr> e(tl::unexpect, make_u_int(21));
    auto ret = std::move(e).or_else(failvoidptr);
    REQUIRE(!ret);
    REQUIRE(*ret.error() == 21);
  }

  {
    tl::expected<int, eptr> e(tl::unexpect, make_u_int(21));
    auto ret = std::move(e).or_else(consumeptr);
    REQUIRE(!ret);
    REQUIRE(ret.error() == nullptr);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).or_else(fail);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 17);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).or_else(failvoid);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

}

TEST_CASE("Transform extensions", "[extensions.tronsfarm]") {
  auto mul2 = [](int a) { return a * 2; };
  auto ret_void = [](int a) { (void)a; };

  {
    tl::expected<int, int> e = 21;
    auto ret = e.transform(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.transform(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).transform(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).transform(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 42);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.transform(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.transform(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).transform(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).transform(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 21);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = e.transform(ret_void);
    REQUIRE(ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.transform(ret_void);
    REQUIRE(ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).transform(ret_void);
    REQUIRE(ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).transform(ret_void);
    REQUIRE(ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.transform(ret_void);
    REQUIRE(!ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.transform(ret_void);
    REQUIRE(!ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).transform(ret_void);
    REQUIRE(!ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).transform(ret_void);
    REQUIRE(!ret);
    STATIC_REQUIRE(
        (std::is_same<decltype(ret), tl::expected<void, int>>::value));
  }


  // mapping functions which return references
  {
    tl::expected<int, int> e(42);
    auto ret = e.transform([](int& i) -> int& { return i; });
    REQUIRE(ret);
    REQUIRE(ret == 42);
  }
}

TEST_CASE("Transform error extensions", "[extensions.transform_error]") {
  auto mul2 = [](int a) { return a * 2; };
  auto ret_void = [](int a) { (void)a; };

  {
    tl::expected<int, int> e = 21;
    auto ret = e.transform_error(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.transform_error(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).transform_error(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).transform_error(mul2);
    REQUIRE(ret);
    REQUIRE(*ret == 21);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.transform_error(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 42);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.transform_error(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 42);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).transform_error(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 42);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).transform_error(mul2);
    REQUIRE(!ret);
    REQUIRE(ret.error() == 42);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = e.transform_error(ret_void);
    REQUIRE(ret);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = e.transform_error(ret_void);
    REQUIRE(ret);
  }

  {
    tl::expected<int, int> e = 21;
    auto ret = std::move(e).transform_error(ret_void);
    REQUIRE(ret);
  }

  {
    const tl::expected<int, int> e = 21;
    auto ret = std::move(e).transform_error(ret_void);
    REQUIRE(ret);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.transform_error(ret_void);
    REQUIRE(!ret);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = e.transform_error(ret_void);
    REQUIRE(!ret);
  }

  {
    tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).transform_error(ret_void);
    REQUIRE(!ret);
  }

  {
    const tl::expected<int, int> e(tl::unexpect, 21);
    auto ret = std::move(e).transform_error(ret_void);
    REQUIRE(!ret);
  }

}

struct S {
    int x;
};

struct F {
    int x;
};

TEST_CASE("14", "[issue.14]") {
    auto res = tl::expected<S,F>{tl::unexpect, F{}};

    (void)res.map_error([](F f) {
        (void)f;
    });
}

TEST_CASE("32", "[issue.32]") {
    int i = 0;
    tl::expected<void, int> a;
    (void)a.map([&i]{i = 42;});
    REQUIRE(i == 42);

    auto x = a.map([]{return 42;});
    REQUIRE(*x == 42);
}
