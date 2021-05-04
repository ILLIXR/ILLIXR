PKG_DEPS = $(shell cat pkg-deps.txt)
LDFLAGS = $(shell pkg-config ${PKG_DEPS} --libs)
include common/common.mk
