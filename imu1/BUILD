package(default_visibility = ["//visibility:private"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_library(
    name = "imu1",
    srcs = ["src"],
	deps = ["//common"],
)

filegroup(
    name = "src",
    srcs = glob(["**"]),
)
