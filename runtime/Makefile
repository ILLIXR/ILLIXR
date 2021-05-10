PKG_DEPS = $(shell cat pkg-deps.txt)
LDFLAGS = -ldl -pthread -lstdc++fs $(shell pkg-config ${PKG_DEPS} --libs)
include common/common.mk
