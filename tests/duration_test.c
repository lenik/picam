#define _POSIX_C_SOURCE 200809L

#include <check.h>
#include <stdint.h>
#include <string.h>

#include "../src/camview_duration.h"

START_TEST(test_duration_seconds_default_unit) {
    uint64_t ns;
    char err[128];

    ck_assert_int_eq(camview_parse_duration_ns("5", &ns, err, sizeof(err)), 0);
    ck_assert_uint_eq(ns, 5ULL * 1000000000ULL);
}
END_TEST

START_TEST(test_duration_ms) {
    uint64_t ns;
    char err[128];

    ck_assert_int_eq(camview_parse_duration_ns("250ms", &ns, err, sizeof(err)), 0);
    ck_assert_uint_eq(ns, 250000000ULL);
}
END_TEST

START_TEST(test_duration_min) {
    uint64_t ns;
    char err[128];

    ck_assert_int_eq(camview_parse_duration_ns("2min", &ns, err, sizeof(err)), 0);
    ck_assert_uint_eq(ns, 120ULL * 1000000000ULL);
}
END_TEST

START_TEST(test_duration_hr) {
    uint64_t ns;
    char err[128];

    ck_assert_int_eq(camview_parse_duration_ns("1hr", &ns, err, sizeof(err)), 0);
    ck_assert_uint_eq(ns, 3600ULL * 1000000000ULL);
}
END_TEST

START_TEST(test_duration_bad_unit) {
    uint64_t ns;
    char err[128];

    ck_assert_int_ne(camview_parse_duration_ns("3fortnights", &ns, err, sizeof(err)), 0);
    ck_assert_uint_gt(strlen(err), 0);
}
END_TEST

static Suite *duration_suite(void) {
    Suite *s = suite_create("duration");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_duration_seconds_default_unit);
    tcase_add_test(tc, test_duration_ms);
    tcase_add_test(tc, test_duration_min);
    tcase_add_test(tc, test_duration_hr);
    tcase_add_test(tc, test_duration_bad_unit);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    Suite *s = duration_suite();
    SRunner *sr = srunner_create(s);
    int failed;

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
