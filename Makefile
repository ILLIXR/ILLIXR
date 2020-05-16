components = open_vins/ timewarp_gl/ gldemo/ ground_truth_slam/ pose_prediction/ offline_imu_cam/ debugview/

.PHONY: %/plugin.dbg.so
%/plugin.dbg.so: %
	$(MAKE) -C $< plugin.dbg.so

.PHONY: %/main.dbg.exe
%/main.dbg.exe: %
	$(MAKE) -C $< main.dbg.exe

run.dbg: runtime/main.dbg.exe $(components:/=/plugin.dbg.so)
	$^

run.opt: runtime/main.opt.exe $(components:/=/plugin.opt.so)
	$^

.PHONY: clean
clean: clean_runtime $(patsubst %,clean_%,$(components))

.PHONY: clean_%
clean_%:
	$(MAKE) -C $(patsubst clean_%,%,$@) clean
