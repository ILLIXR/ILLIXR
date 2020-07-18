.PHONY: plugin.dbg.so
plugin.dbg.so:
	mkdir -p build/ && \
	cd build/ && \
	cmake .. && \
	make "-j$(nproc)" && \
	cd ../ && \
	rm -f plugin.dbg.so && \
	ln -s build/libplugin.so plugin.dbg.so && \
	true

.PHONY: plugin.opt.so
plugin.opt.so:
	mkdir -p build/ && \
	cd build/ && \
	cmake .. -DDNDEBUG=1 -DCMAKE_BUILD_TYPE=Release .. && \
	make "-j$(nproc)" && \
	cd ../ && \
	rm -f plugin.opt.so && \
	ln -s build/libplugin.so plugin.opt.so && \
	true

.PHONY: clean
clean:
	touch build && rm -rf build *.so
