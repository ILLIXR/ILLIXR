plugins    = ground_truth_slam/ offline_imu_cam/ open_vins/ pose_prediction/ gldemo/ timewarp_gl/ debugview/ audio_pipeline/ hologram/

.PHONY: $(plugins:/=/plugin.dbg.so)
$(plugins:/=/plugin.dbg.so):
	$(MAKE) -C $(dir $@) plugin.dbg.so

.PHONY: $(plugins:/=/plugin.opt.so)
$(plugins:/=/plugin.opt.so):
	$(MAKE) -C $(dir $@) plugin.opt.so

.PHONY: runtime/plugin.dbg.so
runtime/plugin.dbg.so:
	$(MAKE) -C $(dir $@) plugin.dbg.so

.PHONY: runtime/plugin.opt.so
runtime/plugin.opt.so:
	$(MAKE) -C $(dir $@) plugin.opt.so

.PHONY: runtime/main.dbg.exe
runtime/main.dbg.exe: runtime
	$(MAKE) -C runtime main.dbg.exe

.PHONY: runtime/main.opt.exe
runtime/main.opt.exe: runtime
	$(MAKE) -C runtime main.opt.exe

.PHONY: run.dbg
run.dbg: runtime/main.dbg.exe $(plugins:/=/plugin.dbg.so) data1
	     runtime/main.dbg.exe $(plugins:/=/plugin.dbg.so)

.PHONY: run.opt
run.opt: runtime/main.opt.exe $(plugins:/=/plugin.opt.so) data1
	     runtime/main.opt.exe $(plugins:/=/plugin.opt.so)

all.dbg.so: runtime/plugin.dbg.so $(plugins:/=/plugin.dbg.so) data1

all.opt.so: runtime/plugin.opt.so $(plugins:/=/plugin.opt.so) data1

.PHONY: gdb
run.gdb: runtime/main.dbg.exe $(plugins:/=/plugin.dbg.so) data1
	gdb -q --args runtime/main.dbg.exe $(plugins:/=/plugin.dbg.so)

data1:
	curl -o data.zip \
		"http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip" && \
	unzip data.zip && \
	rm -rf __MACOSX data1 && \
	mv mav0 data1

.PHONY: tests
tests: runtime/tests $(plugins:/=/tests)

.PHONY: runtime/tests $(plugins:/=/tests)
runtime/tests $(plugins:/=/tests):
	$(MAKE) -C $(dir $@) tests

.PHONY: clean
clean: runtime/clean $(plugins:/=/clean)

.PHONY: runtime/clean $(plugins:/=/clean)
runtime/clean $(plugins:/=/clean):
	$(MAKE) -C $(dir $@) clean

.PHONY: deepclean
deepclean: runtime/deepclean $(plugins:/=/deepclean)
	touch data1 && rm -rf data1

.PHONY: runtime/deepclean $(plugins:/=/deepclean)
runtime/deepclean $(plugins:/=/deepclean):
	$(MAKE) -C $(dir $@) deepclean
