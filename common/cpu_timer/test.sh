#!/bin/bash
set -e

bazel run //include:cpu_timer_minimal_test \
	  --cxxopt='-std=c++11' \
	  --copt='-Wall' \
	  --copt='-Wextra' \
	  --copt='-pthread' \
	  --linkopt='-pthread' \
;

bazel test //test:cpu_timer_test \
	  --cxxopt='-std=c++11' \
	  --copt='-Wall' \
	  --copt='-Wextra' \
	  --copt='-Og' \
	  --copt='-g' \
	  --copt='-fsanitize=address' \
	  --linkopt='-fsanitize=address' \
	  --strip=never \
|| (cat bazel-out/k8-fastbuild/testlogs/test/cpu_timer_test/test.log ; exit 1)
	  # --copt='-fsanitize=thread' \
	  # --linkopt='-fsanitize=thread' \

	  # --aspects clang_tidy/clang_tidy.bzl%clang_tidy_aspect \
	  # --output_groups=report \

bazel run //perf_test:cpu_timer_perf_test \
	  --cxxopt='-std=c++11' \
	  --copt='-Wall' \
	  --copt='-Wextra' \
	  --copt='-DNDEBUG' \
	  --copt='-O3' \
;

${CLANG_TIDY-clang-tidy} test/*.cpp perf_test/*.cpp -- -I.
