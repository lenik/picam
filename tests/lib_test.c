#define _POSIX_C_SOURCE 200809L

#include <check.h>
#include <stdio.h>
#include <string.h>

#include "../src/lib.h"

START_TEST(test_copy_stream_success) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    char buf[64] = {0};
    const char *text = "alpha\nbeta\n";

    ck_assert_ptr_nonnull(in);
    ck_assert_ptr_nonnull(out);
    ck_assert_uint_eq(fwrite(text, 1, strlen(text), in), strlen(text));
    rewind(in);

    ck_assert_int_eq(copy_stream(in, out), 0);
    rewind(out);

    ck_assert_uint_eq(fread(buf, 1, sizeof(buf) - 1, out), strlen(text));
    ck_assert_str_eq(buf, text);

    fclose(in);
    fclose(out);
}
END_TEST

START_TEST(test_copy_file_missing) {
    ck_assert_int_eq(copy_file("picam-test", "/definitely/not/found"), -1);
}
END_TEST

static Suite *lib_suite(void) {
    Suite *s = suite_create("lib");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_copy_stream_success);
    tcase_add_test(tc, test_copy_file_missing);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    Suite *s = lib_suite();
    SRunner *sr = srunner_create(s);
    int failed;

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
