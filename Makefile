plugins    = ground_truth_slam/ offline_imu_cam/ open_vins/ timewarp_gl/ gldemo/ debugview/ audio_pipeline/

.PHONY: %/plugin.dbg.so
%/plugin.dbg.so: %
	$(MAKE) -C $< plugin.dbg.so

.PHONY: %/main.dbg.exe
%/main.dbg.exe: %
	$(MAKE) -C $< main.dbg.exe

.PHONY: run.dbg
run.dbg: runtime/main.dbg.exe $(plugins:/=/plugin.dbg.so) data1
	     runtime/main.dbg.exe $(plugins:/=/plugin.dbg.so)

.PHONY: run.opt
run.opt: runtime/main.opt.exe $(plugins:/=/plugin.opt.so) data1
	     runtime/main.opt.exe $(plugins:/=/plugin.opt.so)

all.dbg.so: runtime/plugin.dbg.so $(plugins:/=/plugin.dbg.so) data1

all.opt.so: runtime/plugin.opt.so $(plugins:/=/plugin.opt.so) data1

.PHONY: %/plugin.dbg.so
gdb:              runtime/main.dbg.exe $(plugins:/=/plugin.dbg.so) data1
	gdb -q --args runtime/main.dbg.exe $(plugins:/=/plugin.dbg.so) -ex 'set stop-on-solib-events 1'

data1:
	curl -o data.zip \
		"http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip" && \
	unzip data.zip && \
	rm -rf __MACOSX data1 && \
	mv mav0 data1

.PHONY: deepclean
deepclean: clean
	touch data1 && rm -rf data1

.PHONY: clean
clean: clean_runtime $(patsubst %,clean_%,$(plugins))

.PHONY: clean_%
clean_%:
	$(MAKE) -C $(patsubst clean_%,%,$@) clean
