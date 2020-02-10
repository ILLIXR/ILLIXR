package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_library(
    name = "common",
    srcs = ["src"],
    linkstatic = 1,
)

filegroup(
    name = "src",
    srcs = glob(["**"]),
)
