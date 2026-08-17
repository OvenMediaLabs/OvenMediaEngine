
// This code is referenced from https://github.com/mariusbancila/croncpp

#pragma once

#include <algorithm>
#include <bitset>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ov
{
	namespace Cron
	{
		constexpr std::time_t INVALID_TIME = static_cast<std::time_t>(-1);
		constexpr size_t INVALID_INDEX = static_cast<size_t>(-1);

		class Expression;

		namespace Detail
		{
			enum class Field
			{
				second,
				minute,
				hour_of_day,
				day_of_week,
				day_of_month,
				month,
				year
			};

			template <typename Traits>
			static bool FindNext(Expression const& cex, std::tm& date, size_t const dot);
		}  // namespace Detail

		struct Exception : public std::runtime_error
		{
		public:
			explicit Exception(std::string_view message)
				: std::runtime_error(message.data())
			{}
		};

		struct CronStandardTraits
		{
			static const uint8_t CRON_MIN_SECONDS = 0;
			static const uint8_t CRON_MAX_SECONDS = 59;

			static const uint8_t CRON_MIN_MINUTES = 0;
			static const uint8_t CRON_MAX_MINUTES = 59;

			static const uint8_t CRON_MIN_HOURS = 0;
			static const uint8_t CRON_MAX_HOURS = 23;

			static const uint8_t CRON_MIN_DAYS_OF_WEEK = 0;
			static const uint8_t CRON_MAX_DAYS_OF_WEEK = 6;

			static const uint8_t CRON_MIN_DAYS_OF_MONTH = 1;
			static const uint8_t CRON_MAX_DAYS_OF_MONTH = 31;

			static const uint8_t CRON_MIN_MONTHS = 1;
			static const uint8_t CRON_MAX_MONTHS = 12;

			static const uint8_t CRON_MAX_YEARS_DIFF = 4;

			static const inline std::vector<std::string> DAYS = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
			static const inline std::vector<std::string> MONTHS = {"NIL", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
		};

		class Expression;

		template <typename Traits = CronStandardTraits>
		static Expression Make(std::string_view expr);

		class Expression
		{
			std::bitset<60> seconds;
			std::bitset<60> minutes;
			std::bitset<24> hours;
			std::bitset<7> days_of_week;
			std::bitset<31> days_of_month;
			std::bitset<12> months;
			std::string expr;

			friend bool operator==(Expression const& e1, Expression const& e2);
			friend bool operator!=(Expression const& e1, Expression const& e2);

			template <typename Traits>
			friend bool Detail::FindNext(Expression const& cex, std::tm& date, size_t const dot);
			friend std::string ToExprString(Expression const& cex);
			friend std::string ToString(Expression const& cex);

			template <typename Traits>
			friend Expression Make(std::string_view expr);
		};

		inline bool operator==(Expression const& e1, Expression const& e2)
		{
			return e1.seconds == e2.seconds &&
				   e1.minutes == e2.minutes &&
				   e1.hours == e2.hours &&
				   e1.days_of_week == e2.days_of_week &&
				   e1.days_of_month == e2.days_of_month &&
				   e1.months == e2.months;
		}

		inline bool operator!=(Expression const& e1, Expression const& e2)
		{
			return !(e1 == e2);
		}

		inline std::string ToString(Expression const& cex)
		{
			return cex.seconds.to_string() + " " +
				   cex.minutes.to_string() + " " +
				   cex.hours.to_string() + " " +
				   cex.days_of_month.to_string() + " " +
				   cex.months.to_string() + " " +
				   cex.days_of_week.to_string();
		}

		inline std::string ToExprString(Expression const& cex)
		{
			return cex.expr;
		}

		namespace utils
		{
			inline std::string ToString(std::tm const& tm)
			{
				std::ostringstream str;
				str.imbue(std::locale(setlocale(LC_ALL, nullptr)));
				str << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
				if (str.fail())
				{
					throw std::runtime_error("Writing date failed!");
				}

				return str.str();
			}

			inline std::string ToUpper(std::string text)
			{
				std::transform(std::begin(text), std::end(text),
							   std::begin(text), static_cast<int (*)(int)>(std::toupper));

				return text;
			}

			static std::vector<std::string> Split(std::string_view text, char const delimiter)
			{
				std::vector<std::string> tokens;
				std::string token;
				std::istringstream tokenStream(text.data());
				while (std::getline(tokenStream, token, delimiter))
				{
					tokens.push_back(token);
				}
				return tokens;
			}

			constexpr inline bool Contains(std::string_view text, char const ch) noexcept
			{
				return std::string_view::npos != text.find_first_of(ch);
			}
		}  // namespace utils

		namespace Detail
		{
			inline uint8_t ToInt(std::string_view text)
			{
				try
				{
					return static_cast<uint8_t>(std::stoul(text.data()));
				}
				catch (std::exception const& ex)
				{
					throw Exception(ex.what());
				}
			}

			static std::string ReplaceOrdinals(std::string text, std::vector<std::string> const& replacement)
			{
				for (size_t i = 0; i < replacement.size(); ++i)
				{
					auto pos = text.find(replacement[i]);
					if (std::string::npos != pos)
					{
						text.replace(pos, 3, std::to_string(i));
					}
				}

				return text;
			}

			static std::pair<uint8_t, uint8_t> MakeRange(
				std::string_view field,
				uint8_t const minval,
				uint8_t const maxval)
			{
				uint8_t first = 0;
				uint8_t last = 0;
				if (field.size() == 1 && field[0] == '*')
				{
					first = minval;
					last = maxval;
				}
				else if (!utils::Contains(field, '-'))
				{
					first = ToInt(field);
					last = first;
				}
				else
				{
					auto parts = utils::Split(field, '-');
					if (parts.size() != 2)
						throw Exception("Specified range requires two fields");

					first = ToInt(parts[0]);
					last = ToInt(parts[1]);
				}

				if (first > maxval || last > maxval)
				{
					throw Exception("Specified range exceeds maximum");
				}
				if (first < minval || last < minval)
				{
					throw Exception("Specified range is less than minimum");
				}
				if (first > last)
				{
					throw Exception("Specified range start exceeds range end");
				}

				return {first, last};
			}

			template <size_t N>
			static void SetField(std::string_view value, std::bitset<N>& target, uint8_t const minval, uint8_t const maxval)
			{
				if (value.length() > 0 && value[value.length() - 1] == ',')
				{
					throw Exception("Value cannot end with comma");
				}

				auto fields = utils::Split(value, ',');
				if (fields.empty())
				{
					throw Exception("Expression parsing error");
				}

				for (auto const& field : fields)
				{
					if (!utils::Contains(field, '/'))
					{
						auto [first, last] = Detail::MakeRange(field, minval, maxval);

						for (uint8_t i = first - minval; i <= last - minval; ++i)
						{
							target.set(i);
						}
					}
					else
					{
						auto parts = utils::Split(field, '/');
						if (parts.size() != 2)
						{
							throw Exception("Incrementer must have two fields");
						}

						auto [first, last] = Detail::MakeRange(parts[0], minval, maxval);

						if (!utils::Contains(parts[0], '-'))
						{
							last = maxval;
						}

						auto delta = Detail::ToInt(parts[1]);
						if (delta <= 0)
						{
							throw Exception("Incrementer must be a positive value");
						}

						for (uint8_t i = first - minval; i <= last - minval; i += delta)
						{
							target.set(i);
						}
					}
				}
			}

			template <typename Traits>
			static void SetDaysOfWeek(std::string value, std::bitset<7>& target)
			{
				auto days = utils::ToUpper(value);
				auto days_replaced = Detail::ReplaceOrdinals(days, Traits::DAYS);

				if (days_replaced.size() == 1 && days_replaced[0] == '?')
				{
					days_replaced[0] = '*';
				}

				SetField(days_replaced, target, Traits::CRON_MIN_DAYS_OF_WEEK, Traits::CRON_MAX_DAYS_OF_WEEK);
			}

			template <typename Traits>
			static void SetDaysOfMonth(std::string value, std::bitset<31>& target)
			{
				if (value.size() == 1 && value[0] == '?')
				{
					value[0] = '*';
				}

				SetField(value, target, Traits::CRON_MIN_DAYS_OF_MONTH, Traits::CRON_MAX_DAYS_OF_MONTH);
			}

			template <typename Traits>
			static void SetMonth(std::string value, std::bitset<12>& target)
			{
				auto month = utils::ToUpper(value);
				auto month_replaced = ReplaceOrdinals(month, Traits::MONTHS);

				SetField(month_replaced, target, Traits::CRON_MIN_MONTHS, Traits::CRON_MAX_MONTHS);
			}

			template <size_t N>
			inline size_t NextSetBit(std::bitset<N> const& target, size_t /*minimum*/, size_t /*maximum*/, size_t offset)
			{
				for (auto i = offset; i < N; ++i)
				{
					if (target.test(i))
					{
						return i;
					}
				}

				return INVALID_INDEX;
			}

			inline void AddToField(std::tm& date, Field const field, int const val)
			{
				switch (field)
				{
					case Field::second:
						date.tm_sec += val;
						break;
					case Field::minute:
						date.tm_min += val;
						break;
					case Field::hour_of_day:
						date.tm_hour += val;
						break;
					case Field::day_of_week:
					case Field::day_of_month:
						date.tm_mday += val;
						break;
					case Field::month:
						date.tm_mon += val;
						break;
					case Field::year:
						date.tm_year += val;
						break;
				}

				if (INVALID_TIME == std::mktime(&date))
				{
					throw Exception("Invalid time expression");
				}
			}

			inline void SetField(
				std::tm& date,
				Field const field,
				int const val)
			{
				switch (field)
				{
					case Field::second:
						date.tm_sec = val;
						break;
					case Field::minute:
						date.tm_min = val;
						break;
					case Field::hour_of_day:
						date.tm_hour = val;
						break;
					case Field::day_of_week:
						date.tm_wday = val;
						break;
					case Field::day_of_month:
						date.tm_mday = val;
						break;
					case Field::month:
						date.tm_mon = val;
						break;
					case Field::year:
						date.tm_year = val;
						break;
				}

				if (INVALID_TIME == std::mktime(&date))
				{
					throw Exception("Invalid time expression");
				}
			}

			inline void ResetField(
				std::tm& date,
				Field const field)
			{
				switch (field)
				{
					case Field::second:
						date.tm_sec = 0;
						break;
					case Field::minute:
						date.tm_min = 0;
						break;
					case Field::hour_of_day:
						date.tm_hour = 0;
						break;
					case Field::day_of_week:
						date.tm_wday = 0;
						break;
					case Field::day_of_month:
						date.tm_mday = 1;
						break;
					case Field::month:
						date.tm_mon = 0;
						break;
					case Field::year:
						date.tm_year = 0;
						break;
				}

				if (INVALID_TIME == std::mktime(&date))
				{
					throw Exception("Invalid time expression");
				}
			}

			inline void ResetAllFields(
				std::tm& date,
				std::bitset<7> const& marked_fields)
			{
				for (size_t i = 0; i < marked_fields.size(); ++i)
				{
					if (marked_fields.test(i))
						ResetField(date, static_cast<Field>(i));
				}
			}

			inline void MarkField(
				std::bitset<7>& orders,
				Field const field)
			{
				if (!orders.test(static_cast<size_t>(field)))
					orders.set(static_cast<size_t>(field));
			}

			template <size_t N>
			static size_t FindNext(
				std::bitset<N> const& target,
				std::tm& date,
				unsigned int const minimum,
				unsigned int const maximum,
				unsigned int const value,
				Field const field,
				Field const next_field,
				std::bitset<7> const& marked_fields)
			{
				auto next_value = NextSetBit(target, minimum, maximum, value);
				if (INVALID_INDEX == next_value)
				{
					AddToField(date, next_field, 1);
					ResetField(date, field);
					next_value = NextSetBit(target, minimum, maximum, 0);
				}

				if (INVALID_INDEX == next_value || next_value != value)
				{
					SetField(date, field, static_cast<int>(next_value));
					ResetAllFields(date, marked_fields);
				}

				return next_value;
			}

			template <typename Traits>
			static size_t FindNextDay(
				std::tm& date,
				std::bitset<31> const& days_of_month,
				size_t day_of_month,
				std::bitset<7> const& days_of_week,
				size_t day_of_week,
				std::bitset<7> const& marked_fields)
			{
				unsigned int count = 0;
				unsigned int maximum = 366;
				while (
					(!days_of_month.test(day_of_month - Traits::CRON_MIN_DAYS_OF_MONTH) ||
					 !days_of_week.test(day_of_week - Traits::CRON_MIN_DAYS_OF_WEEK)) &&
					count++ < maximum)
				{
					AddToField(date, Field::day_of_month, 1);

					day_of_month = date.tm_mday;
					day_of_week = date.tm_wday;

					ResetAllFields(date, marked_fields);
				}

				return day_of_month;
			}

			template <typename Traits>
			static bool FindNext(Expression const& cex,
								 std::tm& date,
								 size_t const dot)
			{
				bool res = true;

				std::bitset<7> marked_fields{0};
				std::bitset<7> empty_list{0};

				unsigned int second = date.tm_sec;
				auto updated_second = FindNext(
					cex.seconds,
					date,
					Traits::CRON_MIN_SECONDS,
					Traits::CRON_MAX_SECONDS,
					second,
					Field::second,
					Field::minute,
					empty_list);

				if (second == updated_second)
				{
					MarkField(marked_fields, Field::second);
				}

				unsigned int minute = date.tm_min;
				auto update_minute = FindNext(
					cex.minutes,
					date,
					Traits::CRON_MIN_MINUTES,
					Traits::CRON_MAX_MINUTES,
					minute,
					Field::minute,
					Field::hour_of_day,
					marked_fields);
				if (minute == update_minute)
				{
					MarkField(marked_fields, Field::minute);
				}
				else
				{
					res = FindNext<Traits>(cex, date, dot);
					if (!res)
						return res;
				}

				unsigned int hour = date.tm_hour;
				auto updated_hour = FindNext(
					cex.hours,
					date,
					Traits::CRON_MIN_HOURS,
					Traits::CRON_MAX_HOURS,
					hour,
					Field::hour_of_day,
					Field::day_of_week,
					marked_fields);
				if (hour == updated_hour)
				{
					MarkField(marked_fields, Field::hour_of_day);
				}
				else
				{
					res = FindNext<Traits>(cex, date, dot);
					if (!res)
						return res;
				}

				unsigned int day_of_week = date.tm_wday;
				unsigned int day_of_month = date.tm_mday;
				auto updated_day_of_month = FindNextDay<Traits>(
					date,
					cex.days_of_month,
					day_of_month,
					cex.days_of_week,
					day_of_week,
					marked_fields);
				if (day_of_month == updated_day_of_month)
				{
					MarkField(marked_fields, Field::day_of_month);
				}
				else
				{
					res = FindNext<Traits>(cex, date, dot);
					if (!res)
						return res;
				}

				unsigned int month = date.tm_mon;
				auto updated_month = FindNext(
					cex.months,
					date,
					Traits::CRON_MIN_MONTHS,
					Traits::CRON_MAX_MONTHS,
					month,
					Field::month,
					Field::year,
					marked_fields);
				if (month != updated_month)
				{
					if (date.tm_year - dot > Traits::CRON_MAX_YEARS_DIFF)
						return false;

					res = FindNext<Traits>(cex, date, dot);
					if (!res)
						return res;
				}

				return res;
			}
		}  // namespace Detail

		template <typename Traits>
		static Expression Make(std::string_view expr)
		{
			Expression cex;

			if (expr.empty())
			{
				throw Exception("Invalid empty cron expression");
			}

			// Remove to empty fields and validation check
			auto fields = utils::Split(expr, ' ');
			fields.erase(std::remove_if(std::begin(fields), std::end(fields), [](std::string_view s) { return s.empty(); }), std::end(fields));
			if (fields.size() != 6)
			{
				throw Exception("cron expression must have six fields");
			}

			Detail::SetField(fields[0], cex.seconds, Traits::CRON_MIN_SECONDS, Traits::CRON_MAX_SECONDS);
			Detail::SetField(fields[1], cex.minutes, Traits::CRON_MIN_MINUTES, Traits::CRON_MAX_MINUTES);
			Detail::SetField(fields[2], cex.hours, Traits::CRON_MIN_HOURS, Traits::CRON_MAX_HOURS);
			Detail::SetDaysOfWeek<Traits>(fields[5], cex.days_of_week);
			Detail::SetDaysOfMonth<Traits>(fields[3], cex.days_of_month);
			Detail::SetMonth<Traits>(fields[4], cex.months);

			cex.expr = expr;

			return cex;
		}

		template <typename Traits = CronStandardTraits>
		static std::tm Next(Expression const& cex, std::tm date)
		{
			time_t original = std::mktime(&date);
			if (INVALID_TIME == original)
			{
				return {};
			}

			if (!Detail::FindNext<Traits>(cex, date, date.tm_year))
			{
				return {};
			}

			time_t calculated = std::mktime(&date);
			if (INVALID_TIME == calculated)
			{
				return {};
			}

			if (calculated == original)
			{
				AddToField(date, Detail::Field::second, 1);
				if (!Detail::FindNext<Traits>(cex, date, date.tm_year))
				{
					return {};
				}
			}

			return date;
		}

		template <typename Traits = CronStandardTraits>
		static std::time_t Next(Expression const& cex, std::time_t const& date)
		{
			std::tm val;
			std::tm* dt = localtime_r(&date, &val);
			if (dt == nullptr)
			{
				return INVALID_TIME;
			}

			time_t original = std::mktime(dt);
			if (INVALID_TIME == original)
			{
				return INVALID_TIME;
			}

			if (!Detail::FindNext<Traits>(cex, *dt, dt->tm_year))
			{
				return INVALID_TIME;
			}

			time_t calculated = std::mktime(dt);
			if (INVALID_TIME == calculated)
			{
				return calculated;
			}

			if (calculated == original)
			{
				AddToField(*dt, Detail::Field::second, 1);
				if (!Detail::FindNext<Traits>(cex, *dt, dt->tm_year))
				{
					return INVALID_TIME;
				}
			}

			return std::mktime(dt);
		}
	}  // namespace Cron
}  // namespace ov