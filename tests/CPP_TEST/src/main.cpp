#include <ztest.h>


static void test_assert(void)
{
    zassert_true(1, "1 was false");
    zassert_false(0, "0 was true");
    zassert_is_null(NULL, "NULL was not NULL");
    zassert_not_null("foo", "\"foo\" was NULL");
    zassert_equal(1, 1, "1 was not equal to 1");
    zassert_equal_ptr(NULL, NULL, "NULL was not equal to NULL");
}


static void throw_exception(void)
{
    throw 42;
}


static void test_cexception(void)
{
    // try
    // {
        throw_exception();
    // }
    // catch (int i)
    // {
        // zassert_equal(i, 42U, "Incorrect exception value");
        // return;
    // }

    zassert_true(0, "Missing exception catch");
}




void test_main(void)
{
    ztest_test_suite(framework_tests,
                     ztest_unit_test(test_assert),
                     ztest_unit_test(test_cexception));

    ztest_run_test_suite(framework_tests);
}
