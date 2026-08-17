#include <catch2/catch.hpp>
#include <tl/expected.hpp>

#include <string>

// Old versions of GCC don't have the correct trait names. Could fix them up if needs be.
#if (defined(__GNUC__) && __GNUC__ == 4 && __GNUC_MINOR__ <= 9 &&              \
     !defined(__clang__))
// nothing for now
#else
TEST_CASE("Triviality", "[bases.triviality]") {
    REQUIRE(std::is_trivially_copy_constructible<tl::expected<int,int>>::value);
    REQUIRE(std::is_trivially_copy_assignable<tl::expected<int,int>>::value);
    REQUIRE(std::is_trivially_move_constructible<tl::expected<int,int>>::value);
    REQUIRE(std::is_trivially_move_assignable<tl::expected<int,int>>::value);
    REQUIRE(std::is_trivially_destructible<tl::expected<int,int>>::value);

    REQUIRE(std::is_trivially_copy_constructible<tl::expected<void,int>>::value);
    REQUIRE(std::is_trivially_move_constructible<tl::expected<void,int>>::value);
    REQUIRE(std::is_trivially_destructible<tl::expected<void,int>>::value);


    {
        struct T {
            T(const T&) = default;
            T(T&&) = default;
            T& operator=(const T&) = default;
            T& operator=(T&&) = default;
            ~T() = default;
        };
        REQUIRE(std::is_trivially_copy_constructible<tl::expected<T,int>>::value);
        REQUIRE(std::is_trivially_copy_assignable<tl::expected<T,int>>::value);
        REQUIRE(std::is_trivially_move_constructible<tl::expected<T,int>>::value);
        REQUIRE(std::is_trivially_move_assignable<tl::expected<T,int>>::value);
        REQUIRE(std::is_trivially_destructible<tl::expected<T,int>>::value);
    }

    {
        struct T {
            T(const T&){}
            T(T&&) {}
            T& operator=(const T&) { return *this; }
            T& operator=(T&&) { return *this; }
            ~T(){}
        };
        REQUIRE(!std::is_trivially_copy_constructible<tl::expected<T,int>>::value);
        REQUIRE(!std::is_trivially_copy_assignable<tl::expected<T,int>>::value);
        REQUIRE(!std::is_trivially_move_constructible<tl::expected<T,int>>::value);
        REQUIRE(!std::is_trivially_move_assignable<tl::expected<T,int>>::value);
        REQUIRE(!std::is_trivially_destructible<tl::expected<T,int>>::value);
    }

}

TEST_CASE("Deletion", "[bases.deletion]") {
    REQUIRE(std::is_copy_constructible<tl::expected<int,int>>::value);
    REQUIRE(std::is_copy_assignable<tl::expected<int,int>>::value);
    REQUIRE(std::is_move_constructible<tl::expected<int,int>>::value);
    REQUIRE(std::is_move_assignable<tl::expected<int,int>>::value);
    REQUIRE(std::is_destructible<tl::expected<int,int>>::value);

    {
        struct T {
            T()=default;
        };
        REQUIRE(std::is_default_constructible<tl::expected<T,int>>::value);
    }

    {
        struct T {
            T(int){}
        };
        REQUIRE(!std::is_default_constructible<tl::expected<T,int>>::value);
    }

    {
        struct T {
            T(const T&) = default;
            T(T&&) = default;
            T& operator=(const T&) = default;
            T& operator=(T&&) = default;
            ~T() = default;
        };
        REQUIRE(std::is_copy_constructible<tl::expected<T,int>>::value);
        REQUIRE(std::is_copy_assignable<tl::expected<T,int>>::value);
        REQUIRE(std::is_move_constructible<tl::expected<T,int>>::value);
        REQUIRE(std::is_move_assignable<tl::expected<T,int>>::value);
        REQUIRE(std::is_destructible<tl::expected<T,int>>::value);
    }

    {
        struct T {
            T(const T&)=delete;
            T(T&&)=delete;
            T& operator=(const T&)=delete;
            T& operator=(T&&)=delete;
        };
        REQUIRE(!std::is_copy_constructible<tl::expected<T,int>>::value);
        REQUIRE(!std::is_copy_assignable<tl::expected<T,int>>::value);
        REQUIRE(!std::is_move_constructible<tl::expected<T,int>>::value);
        REQUIRE(!std::is_move_assignable<tl::expected<T,int>>::value);
    }

    {
        struct T {
            T(const T&)=delete;
            T(T&&)=default;
            T& operator=(const T&)=delete;
            T& operator=(T&&)=default;
        };
        REQUIRE(!std::is_copy_constructible<tl::expected<T,int>>::value);
        REQUIRE(!std::is_copy_assignable<tl::expected<T,int>>::value);
        REQUIRE(std::is_move_constructible<tl::expected<T,int>>::value);
        REQUIRE(std::is_move_assignable<tl::expected<T,int>>::value);
    }

    {
        struct T {
            T(const T&)=default;
            T(T&&)=delete;
            T& operator=(const T&)=default;
            T& operator=(T&&)=delete;
        };
        REQUIRE(std::is_copy_constructible<tl::expected<T,int>>::value);
        REQUIRE(std::is_copy_assignable<tl::expected<T,int>>::value);
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

}


#endif
