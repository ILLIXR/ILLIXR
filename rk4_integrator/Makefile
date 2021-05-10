PKG_DEPS = $(shell cat pkg-deps.txt)
LDFLAGS = $(shell pkg-config ${PKG_DEPS} --libs)
CPPFLAGS = $(shell pkg-config ${PKG_DEPS} --cflags)
include common/common.mk
