package(default_visibility = ["//visibility:private"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "main",
    srcs = ["src"],
    linkopts = ["-pthread", "-ldl"],
	deps = ["//common"],
    copts = ["-std=c++2a", "-stdlib=libc++"],
)

filegroup(
    name = "src",
    srcs = glob(["**"]),
)
