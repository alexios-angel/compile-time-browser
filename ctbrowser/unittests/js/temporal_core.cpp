//===- ctbrowser/unittests/js/temporal_core.cpp - Temporal core tests -----===//
//
// Temporal's core on plain records (the ISO calendar, rounding, the string
// forms, the RFC 9557 grammar and the relative-rounding machinery) with no VM
// in the way (lib/Script/builtins/temporal/core.hpp). The expected values are
// the specification's and test262's answers; the classes built on these are
// measured by test262's built-ins/Temporal.
//
//===----------------------------------------------------------------------===//

#include "../../lib/Script/builtins/temporal/core.hpp"

#include "check.hpp"
#include <cstdio>
#include <string>

using namespace ctbrowser::script::temporal;

#define EXPECT(cond) CHECK(cond)
#define EXPECT_STR(a, b) CHECK_EQ(std::string{a}, std::string{b})

/// Formats a date duration as "years/months/weeks/days".
static std::string dd(const date_duration & d) {
    return integer_to_string(d.years) + "/" + integer_to_string(d.months) + "/" +
           integer_to_string(d.weeks) + "/" + integer_to_string(d.days);
}

int main() {
    // Calendar.
    EXPECT(epoch_days(iso_date{1970, 1, 1}) == 0);
    EXPECT(epoch_days(iso_date{2000, 3, 1}) == 11017);
    EXPECT(epoch_days_to_iso_date(-1) == (iso_date{1969, 12, 31}));
    EXPECT(epoch_days_to_iso_date(epoch_days(iso_date{-271821, 4, 19})) ==
           (iso_date{-271821, 4, 19}));
    EXPECT(iso_day_of_week(iso_date{2024, 1, 1}) == 1);
    EXPECT(iso_day_of_week(iso_date{2023, 12, 31}) == 7);
    EXPECT(iso_week_of_year(iso_date{2021, 1, 1}).week == 53);
    EXPECT(iso_week_of_year(iso_date{2021, 1, 1}).year == 2020);
    EXPECT(iso_week_of_year(iso_date{2019, 12, 30}).week == 1);
    EXPECT(iso_day_of_year(iso_date{2020, 12, 31}) == 366);
    EXPECT(iso_date_within_limits(iso_date{-271821, 4, 19}));
    EXPECT(!iso_date_within_limits(iso_date{-271821, 4, 18}));
    EXPECT(iso_date_within_limits(iso_date{275760, 9, 13}));
    EXPECT(!iso_date_within_limits(iso_date{275760, 9, 14}));

    // CalendarDateUntil.
    EXPECT_STR(dd(calendar_date_until({2020, 1, 31}, {2020, 2, 29}, unit::month)), "0/0/0/29");
    EXPECT_STR(dd(calendar_date_until({2020, 1, 31}, {2020, 3, 1}, unit::month)), "0/1/0/1");
    EXPECT_STR(dd(calendar_date_until({2019, 1, 1}, {2021, 3, 15}, unit::year)), "2/2/0/14");
    EXPECT_STR(dd(calendar_date_until({2021, 3, 15}, {2019, 1, 1}, unit::year)), "-2/-2/0/-14");
    EXPECT_STR(dd(calendar_date_until({2020, 1, 1}, {2020, 3, 1}, unit::week)), "0/0/8/4");
    EXPECT_STR(dd(calendar_date_until({2020, 1, 1}, {2020, 3, 1}, unit::day)), "0/0/0/60");
    EXPECT_STR(dd(calendar_date_until({2020, 3, 31}, {2020, 2, 29}, unit::month)), "0/-1/0/0");
    EXPECT_STR(dd(calendar_date_until({2020, 2, 29}, {2021, 2, 28}, unit::year)), "0/11/0/30");

    // CalendarDateAdd.
    {
        const auto r =
            calendar_date_add({2020, 1, 31}, date_duration{0, 1, 0, 0}, overflow::constrain);
        EXPECT(r && *r == (iso_date{2020, 2, 29}));
        EXPECT(!calendar_date_add({2020, 1, 31}, date_duration{0, 1, 0, 0}, overflow::reject));
    }

    // Rounding.
    EXPECT(round_number_to_increment(15, 10, rounding_mode::half_even) == 20);
    EXPECT(round_number_to_increment(25, 10, rounding_mode::half_even) == 20);
    EXPECT(round_number_to_increment(-15, 10, rounding_mode::half_ceil) == -10);
    EXPECT(round_number_to_increment(-15, 10, rounding_mode::floor) == -20);
    EXPECT(round_number_to_increment(-15, 10, rounding_mode::half_floor) == -20);
    EXPECT(round_number_to_increment_as_if_positive(-15, 10, rounding_mode::trunc) == -20);
    EXPECT(exact_quotient(1, 3) == 1.0 / 3.0);
    EXPECT(exact_quotient(-7, 2) == -3.5);
    EXPECT(exact_quotient((i128{1} << 80) + 1, 1) == 1208925819614629174706176.0);

    // Strings.
    EXPECT_STR(pad_iso_year(-1), "-000001");
    EXPECT_STR(pad_iso_year(12345), "+012345");
    EXPECT_STR(pad_iso_year(976), "0976");
    EXPECT_STR(format_time_string(1, 2, 3, 400'000'000, precision_auto), "01:02:03.4");
    EXPECT_STR(format_time_string(1, 2, 3, 0, 3), "01:02:03.000");
    EXPECT_STR(format_utc_offset_nanoseconds(-(i128{5} * 3'600'000'000'000 + 1)),
               "-05:00:00.000000001");
    EXPECT_STR(instant_to_string(0, "", precision_auto), "1970-01-01T00:00:00Z");
    EXPECT_STR(instant_to_string(-1, "+01:00", precision_auto),
               "1970-01-01T00:59:59.999999999+01:00");

    // Durations.
    {
        duration_record d{};
        d.hours = 1;
        d.minutes = 30;
        EXPECT_STR(temporal_duration_to_string(d, precision_auto), "PT1H30M");
        EXPECT_STR(temporal_duration_to_string(duration_record{}, precision_auto), "PT0S");
        duration_record n{};
        n.days = -1;
        n.milliseconds = -500;
        EXPECT_STR(temporal_duration_to_string(n, precision_auto), "-P1DT0.5S");
        duration_record big{};
        big.nanoseconds = 9007199254740992e9;
        EXPECT(!is_valid_duration(big));
        duration_record limit{};
        limit.seconds = 9007199254740991.0;
        EXPECT(is_valid_duration(limit));
        limit.nanoseconds = 999999999;
        EXPECT(is_valid_duration(limit));
        limit.nanoseconds = 1e9;
        EXPECT(!is_valid_duration(limit));
    }
    {
        const auto d = parse_temporal_duration_string("PT46H66M71.50040904S");
        EXPECT(d && d->hours == 46 && d->minutes == 66 && d->seconds == 71 &&
               d->milliseconds == 500 && d->microseconds == 409 && d->nanoseconds == 40);
        const auto h = parse_temporal_duration_string("-PT1.5H");
        EXPECT(h && h->hours == -1 && h->minutes == -30);
        const auto m = parse_temporal_duration_string("PT0.123456789M");
        EXPECT(m && m->seconds == 7 && m->milliseconds == 407 && m->microseconds == 407 &&
               m->nanoseconds == 340);
        EXPECT(!parse_temporal_duration_string("P"));
        EXPECT(!parse_temporal_duration_string("PT"));
        EXPECT(!parse_temporal_duration_string("P1.5D"));
        EXPECT(!parse_temporal_duration_string("PT1.5H1M"));
        EXPECT(parse_temporal_duration_string("p1y2m3w4dt5h6m7s"));
        EXPECT(!parse_temporal_duration_string("P1D 1H"));
    }

    // The grammar.
    {
        const auto r = parse_iso_datetime(
            "2020-01-01T12:34:56.123456789+01:00[Europe/Paris][u-ca=iso8601]", {goal::date_time});
        EXPECT(r && r->year == 2020 && r->time && r->time->nanosecond == 789 &&
               r->offset == "+01:00" && r->annotation == "Europe/Paris" &&
               r->calendar == "iso8601");
        EXPECT(!parse_iso_datetime("2020-01-01T00:00Z", {goal::date_time}));
        EXPECT(parse_iso_datetime("2020-01-01T00:00Z", {goal::instant}));
        EXPECT(!parse_iso_datetime("2020-01-01", {goal::instant}));
        EXPECT(parse_iso_datetime("19700101T012345", {goal::date_time}));
        EXPECT(!parse_iso_datetime("1970-0101T012345", {goal::date_time}));
        EXPECT(!parse_iso_datetime("1970-01-01T0123:45", {goal::date_time}));
        EXPECT(!parse_iso_datetime("-000000-01-01", {goal::date_time}));
        EXPECT(!parse_iso_datetime("2021-02-29", {goal::date_time}));
        EXPECT(parse_iso_datetime("2020-02-29", {goal::date_time}));
        EXPECT(!parse_iso_datetime("2020-01-01[!foo=bar]", {goal::date_time}));
        EXPECT(parse_iso_datetime("2020-01-01[foo=bar]", {goal::date_time}));
        EXPECT(!parse_iso_datetime("2020-01-01[u-ca=iso8601][!u-ca=gregory]", {goal::date_time}));
        EXPECT(parse_iso_datetime("2020-01-01[u-ca=iso8601][u-ca=gregory]", {goal::date_time}));
        EXPECT(!parse_iso_datetime("2020-01-01[FOO=bar]", {goal::date_time}));
        // Times, and the ambiguity with month-days and year-months.
        EXPECT(parse_iso_datetime("T1214", {goal::time}));
        EXPECT(!parse_iso_datetime("1214", {goal::time}));
        EXPECT(!parse_iso_datetime("2021-12", {goal::time}));
        EXPECT(parse_iso_datetime("12:14", {goal::time}));
        EXPECT(!parse_iso_datetime("1261", {goal::time})); // 12:61 is not a time.
        const auto md = parse_iso_datetime("--12-25", {goal::month_day});
        EXPECT(md && !md->year && md->month == 12 && md->day == 25);
        const auto ym = parse_iso_datetime("202112", {goal::year_month});
        EXPECT(ym && ym->year == 2021 && ym->month == 12);
        EXPECT(parse_iso_datetime("2020-01-01T00:00:60", {goal::date_time}));
        EXPECT(parse_iso_datetime("2020-01-01 00:00", {goal::date_time}));
        EXPECT(!parse_iso_datetime("2020-01-01T24:00", {goal::date_time}));
        const auto zoned =
            parse_iso_datetime("2020-01-01T00:00+00:00[UTC]", {goal::date_time_zoned});
        EXPECT(zoned && zoned->annotation == "UTC");
        EXPECT(!parse_iso_datetime("2020-01-01T00:00+00:00", {goal::date_time_zoned}));
    }
    {
        const auto o = parse_utc_offset("+01:00:00.5");
        EXPECT(o && *o == i128{3'600'500'000'000});
        EXPECT(!parse_utc_offset("+01:00:00", false));
        EXPECT(!parse_utc_offset("+0100:00"));
        const auto z = parse_temporal_time_zone_string("2020-01-01T00:00Z");
        EXPECT(z && z->name == "UTC");
        const auto off = parse_time_zone_identifier("-05:30");
        EXPECT(off && off->offset_minutes && *off->offset_minutes == -330);
        EXPECT_STR(*canonical_time_zone(*parse_time_zone_identifier("-0530")), "-05:30");
        EXPECT(!canonical_time_zone(*parse_time_zone_identifier("Europe/Paris")));
        EXPECT_STR(*canonical_time_zone(*parse_time_zone_identifier("utc")), "UTC");
    }

    // Relative rounding: 2020-01-01 until 2020-03-15, to the nearest month.
    {
        const iso_datetime a{{2020, 1, 1}, {}};
        const iso_datetime b{{2020, 3, 15}, {}};
        const auto r = difference_plain_datetime_with_rounding(a, b, unit::year, 1, unit::month,
                                                               rounding_mode::half_expand);
        EXPECT(r && r->date.months == 2 && r->date.days == 0 && r->time == 0);
        const auto up =
            difference_plain_datetime_with_rounding(a, iso_datetime{{2020, 3, 17}, {}}, unit::year,
                                                    1, unit::month, rounding_mode::half_expand);
        EXPECT(up && up->date.months == 3);
        const auto total = difference_plain_datetime_with_total(a, b, unit::month);
        EXPECT(total && *total > 2.45 && *total < 2.46);
        // 11 months 20 days rounds up to a year and bubbles.
        const auto year =
            difference_plain_datetime_with_rounding(a, iso_datetime{{2020, 12, 21}, {}}, unit::year,
                                                    1, unit::month, rounding_mode::half_expand);
        EXPECT(year && year->date.years == 1 && year->date.months == 0);
    }
    // Time-only rounding with days: 1 day 23:59:59.5 to seconds, largest day.
    {
        const iso_datetime a{{2020, 1, 1}, {}};
        const iso_datetime b{{2020, 1, 2}, {0, 23, 59, 59, 500, 0, 0}};
        const auto r = difference_plain_datetime_with_rounding(a, b, unit::day, 1, unit::second,
                                                               rounding_mode::half_expand);
        EXPECT(r && r->date.days == 2 && r->time == 0);
    }
    REPORT("temporal_core");
}
