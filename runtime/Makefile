CFLAGS = $(shell pkg-config opencv --cflags)
LDFLAGS = -ldl -pthread -lstdc++fs $(shell pkg-config glfw3 glew sqlite3 x11 --libs) $(shell pkg-config opencv --libs)
include common/common.mk
