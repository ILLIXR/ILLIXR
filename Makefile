components = ground_truth_slam/ offline_imu_cam/ open_vins/ pose_prediction/ timewarp_gl/ gldemo/ debugview/

.PHONY: %/plugin.dbg.so
%/plugin.dbg.so: %
	$(MAKE) -C $< plugin.dbg.so

.PHONY: %/main.dbg.exe
%/main.dbg.exe: %
	$(MAKE) -C $< main.dbg.exe

.PHONY: run.dbg
run.dbg: runtime/main.dbg.exe $(components:/=/plugin.dbg.so)
	$^

.PHONY: run.opt
run.opt: runtime/main.opt.exe $(components:/=/plugin.opt.so)
	$^

.PHONY: %/plugin.dbg.so
gdb: runtime/main.dbg.exe $(components:/=/plugin.dbg.so)
	@echo "gdb> run $(components:/=/plugin.dbg.so)" && \
	gdb -q --args $^

# gdb.opt does not make sense, since opt does not contain debug symbols

.PHONY: clean
clean: clean_runtime $(patsubst %,clean_%,$(components))

.PHONY: clean_%
clean_%:
	$(MAKE) -C $(patsubst clean_%,%,$@) clean
