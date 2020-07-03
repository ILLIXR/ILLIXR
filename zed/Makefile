.PHONY: plugin.dbg.so
plugin.dbg.so:
	mkdir -p zed-opencv/cpp/build/ && \
	cd zed-opencv/cpp/build/ && \
	cmake .. && \
	make "-j$(nproc)" && \
	cd ../../.. && \
	rm -f plugin.dbg.so && \
	ln -s zed-opencv/cpp/build/libplugin.so plugin.dbg.so && \
	true

.PHONY: plugin.opt.so
plugin.opt.so:
	mkdir -p zed-opencv/cpp/build/ && \
	cd zed-opencv/cpp/build/ && \
	cmake .. -DDNDEBUG=1 -DCMAKE_BUILD_TYPE=Release .. && \
	make "-j$(nproc)" && \
	cd ../../.. && \
	rm -f plugin.opt.so && \
	ln -s zed-opencv/cpp/build/libplugin.so plugin.opt.so && \
	true

.PHONY: clean
clean:
	touch zed-opencv/cpp/build && rm -rf zed-opencv/cpp/build *.so
