package(default_visibility = ["//visibility:private"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_library(
    name = "slam3",
    srcs = ["src"],
	deps = ["//common"],
    copts = ["--std=c++2a", "-I/usr/include/python3.7m", "-I/usr/include/python3.7m", "-Wno-unused-result", "-Wsign-compare", "-g", "-fdebug-prefix-map=/build/python3.7-9TOSzB/python3.7-3.7.6=.", "-specs=/usr/share/dpkg/no-pie-compile.specs", "-fstack-protector", "-Wformat", "-Werror=format-security", "-DNDEBUG", "-g", "-fwrapv", "-O3", "-Wall"],
    linkopts = ["-L/usr/lib/python3.7/config-3.7m-x86_64-linux-gnu", "-L/usr/lib", "-lpython3.7m", "-lcrypt", "-lpthread", "-ldl", "-lutil", "-lm"],
)

filegroup(
    name = "src",
    srcs = glob(["**"]),
)
