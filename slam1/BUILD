package(default_visibility = ["//visibility:private"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_library(
    name = "slam1",
    srcs = ["src"],
	deps = ["//common"],
    copts = ["--std=c++2a"]
)

filegroup(
    name = "src",
    srcs = glob(["**"]),
)
