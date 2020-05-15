components = open_vins/ timewarp_gl/ gldemo/ ground_truth_slam/ pose_prediction/ offline_imu_cam/ debugview/

.PHONY: %/plugin.dbg.so
%/plugin.dbg.so: %
	$(MAKE) -C $< plugin.dbg.so

.PHONY: %/main.dbg.exe
%/main.dbg.exe: %
	$(MAKE) -C $< main.dbg.exe

dbg: runtime/main.dbg.exe $(components:/=/plugin.dbg.so)
	$^
