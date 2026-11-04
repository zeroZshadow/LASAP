#pragma once
// Code of the test build only (LASAP_TEST): TEST_ONLY(<statements or declarations>) is that code there and nothing in
// the release module.
#ifdef LASAP_TEST
#define TEST_ONLY(...) __VA_ARGS__
#else
#define TEST_ONLY(...)
#endif
