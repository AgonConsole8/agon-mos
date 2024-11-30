#ifndef MOS_TESTS_H
#define MOS_TESTS_H

#include "defines.h"

#if DEBUG > 0

void malloc_grind(bool verbose);

void path_tests(bool verbose);

void string_tests(bool verbose);

#endif /* DEBUG */

#endif /* MOS_TESTS_H */
