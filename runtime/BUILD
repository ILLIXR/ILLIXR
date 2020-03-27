package(default_visibility = ["//visibility:private"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "main",
    srcs = ["src"],
    linkopts = ["-pthread", "-ldl", "-lglfw -lrt -lm -ldl -lGLEW -lGLU -lm -lGL -lpthread -pthread -lm -ldl -lX11-xcb -lxcb-glx -ldrm -lXdamage -lXfixes -lxcb-dri2 -lXxf86vm -lXext -lX11 -lpthread -lxcb -lXau -lXdmcp"],
	deps = ["//common"],
    copts = ["-std=c++2a", "-stdlib=libc++"],
)

filegroup(
    name = "src",
    srcs = glob(["**"]),
)
