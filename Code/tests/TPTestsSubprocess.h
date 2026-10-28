#pragma once

#include <string_view>

/** Runs one exact Catch test case in a fresh TPTests process. */
int RunTPTestsSubprocess(std::string_view aTestName);
