#include <catch2/catch.hpp>
#include <tl/expected.hpp>

#include <type_traits>
#include <vector>
#include <string>

struct takes_init_and_variadic {
    std::vector<int> v;
    std::tuple<int, int> t;
    template <class... Args>
    takes_init_and_variadic(std::initializer_list<int> l, Args &&... args)
        : v(l), t(std::forward<Args>(args)...) {}
};

TEST_CASE("Constructors", "[constructors]") {
    {
        tl::expected<int,int> e;
        REQUIRE(e);
        REQUIRE(e == 0);
    }

    {
        tl::expected<int,int> e = tl::make_unexpected(0);
        REQUIRE(!e);
        REQUIRE(e.error() == 0);
    }

    {
        tl::expected<int,int> e (tl::unexpect, 0);
        REQUIRE(!e);
        REQUIRE(e.error() == 0);
    }

    {
        tl::expected<int,int> e (tl::in_place, 42);
        REQUIRE(e);
        REQUIRE(e == 42);
    }

    {
        tl::expected<std::vector<int>,int> e (tl::in_place, {0,1});
        REQUIRE(e);
        REQUIRE((*e)[0] == 0);
        REQUIRE((*e)[1] == 1);
    }

    {
        tl::expected<std::tuple<int,int>,int> e (tl::in_place, 0, 1);
        REQUIRE(e);
        REQUIRE(std::get<0>(*e) == 0);
        REQUIRE(std::get<1>(*e) == 1);
    }

    {
        tl::expected<takes_init_and_variadic,int> e (tl::in_place, {0,1}, 2, 3);
        REQUIRE(e);
        REQUIRE(e->v[0] == 0);
        REQUIRE(e->v[1] == 1);
        REQUIRE(std::get<0>(e->t) == 2);
        REQUIRE(std::get<1>(e->t) == 3);
    }

	{
		tl::expected<int, int> e;
		REQUIRE(std::is_default_constructible<decltype(e)>::value);
		REQUIRE(std::is_copy_constructible<decltype(e)>::value);
		REQUIRE(std::is_move_constructible<decltype(e)>::value);
		REQUIRE(std::is_copy_assignable<decltype(e)>::value);
		REQUIRE(std::is_move_assignable<decltype(e)>::value);
		REQUIRE(TL_EXPECTED_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(decltype(e))::value);
		REQUIRE(TL_EXPECTED_IS_TRIVIALLY_COPY_ASSIGNABLE(decltype(e))::value);
#	if !defined(TL_EXPECTED_GCC49)
		REQUIRE(std::is_trivially_move_constructible<decltype(e)>::value);
		REQUIRE(std::is_trivially_move_assignable<decltype(e)>::value);
#	endif
	}

	{
		tl::expected<int, std::string> e;
		REQUIRE(std::is_default_constructible<decltype(e)>::value);
		REQUIRE(std::is_copy_constructible<decltype(e)>::value);
		REQUIRE(std::is_move_constructible<decltype(e)>::value);
		REQUIRE(std::is_copy_assignable<decltype(e)>::value);
		REQUIRE(std::is_move_assignable<decltype(e)>::value);
		REQUIRE(!TL_EXPECTED_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(decltype(e))::value);
		REQUIRE(!TL_EXPECTED_IS_TRIVIALLY_COPY_ASSIGNABLE(decltype(e))::value);
#	if !defined(TL_EXPECTED_GCC49)
		REQUIRE(!std::is_trivially_move_constructible<decltype(e)>::value);
		REQUIRE(!std::is_trivially_move_assignable<decltype(e)>::value);
#	endif
	}

	{
		tl::expected<std::string, int> e;
		REQUIRE(std::is_default_constructible<decltype(e)>::value);
		REQUIRE(std::is_copy_constructible<decltype(e)>::value);
		REQUIRE(std::is_move_constructible<decltype(e)>::value);
		REQUIRE(std::is_copy_assignable<decltype(e)>::value);
		REQUIRE(std::is_move_assignable<decltype(e)>::value);
		REQUIRE(!TL_EXPECTED_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(decltype(e))::value);
		REQUIRE(!TL_EXPECTED_IS_TRIVIALLY_COPY_ASSIGNABLE(decltype(e))::value);
#	if !defined(TL_EXPECTED_GCC49)
		REQUIRE(!std::is_trivially_move_constructible<decltype(e)>::value);
		REQUIRE(!std::is_trivially_move_assignable<decltype(e)>::value);
#	endif
	}

	{
		tl::expected<std::string, std::string> e;
		REQUIRE(std::is_default_constructible<decltype(e)>::value);
		REQUIRE(std::is_copy_constructible<decltype(e)>::value);
		REQUIRE(std::is_move_constructible<decltype(e)>::value);
		REQUIRE(std::is_copy_assignable<decltype(e)>::value);
		REQUIRE(std::is_move_assignable<decltype(e)>::value);
		REQUIRE(!TL_EXPECTED_IS_TRIVIALLY_COPY_CONSTRUCTIBLE(decltype(e))::value);
		REQUIRE(!TL_EXPECTED_IS_TRIVIALLY_COPY_ASSIGNABLE(decltype(e))::value);
#	if !defined(TL_EXPECTED_GCC49)
		REQUIRE(!std::is_trivially_move_constructible<decltype(e)>::value);
		REQUIRE(!std::is_trivially_move_assignable<decltype(e)>::value);
#	endif
	}

    {
        tl::expected<void,int> e;
        REQUIRE(e);
    }

    {
        tl::expected<void,int> e (tl::unexpect, 42);
        REQUIRE(!e);
        REQUIRE(e.error() == 42);
    }
}
