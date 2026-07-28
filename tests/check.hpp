#ifndef CTBROWSER_V2_TEST_CHECK_HPP
#define CTBROWSER_V2_TEST_CHECK_HPP

#include <cstdio>

// Same shape as the previous engine's tests: a non-zero exit fails ctest, and every failure
// prints its own file:line so a CI log says what broke without a debugger.
inline int ctbrowser_test_failures = 0;

#define CHECK(cond)                                                                                \
	do {                                                                                           \
		if (!(cond)) {                                                                             \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
			++ctbrowser_test_failures;                                                             \
		}                                                                                          \
	} while (0)

#define CHECK_EQ(a, b)                                                                             \
	do {                                                                                           \
		const auto lhs_ = (a);                                                                     \
		const auto rhs_ = (b);                                                                     \
		if (!(lhs_ == rhs_)) {                                                                     \
			std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);                     \
			++ctbrowser_test_failures;                                                             \
		}                                                                                          \
	} while (0)

#define REPORT(name)                                                                               \
	do {                                                                                           \
		if (ctbrowser_test_failures == 0) { std::printf("ok %s\n", name); }                        \
		return ctbrowser_test_failures == 0 ? 0 : 1;                                               \
	} while (0)

#endif
