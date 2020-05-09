package(default_visibility = ["//visibility:private"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_library(
    name = "pose_prediction",
    srcs = ["src"],
    hdrs = ["kalman.cc"],
    linkopts = [],
	deps = ["//common"],
    copts = ["--std=c++2a", "-I/usr/include/eigen3"],
)

filegroup(
    name = "src",
    srcs = glob(["**"]),
)
