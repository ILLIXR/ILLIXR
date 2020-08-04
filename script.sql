.header on
.mode column
-- .mode csv
-- .output.csv

ATTACH "metrics/plugin_start.sqlite" AS p0;
ATTACH "metrics/threadloop_iteration_start.sqlite" AS p1;
ATTACH "metrics/threadloop_iteration_stop.sqlite" AS p2;
ATTACH "metrics/switchboard_callback_start.sqlite" AS p3;
ATTACH "metrics/switchboard_callback_stop.sqlite" AS p4;
ATTACH "metrics/threadloop_skip_start.sqlite" AS p5;
ATTACH "metrics/threadloop_skip_stop.sqlite" AS p6;

.print "\n## All registered plugins ##"
SELECT plugin_id, plugin_name
FROM plugin_start
ORDER BY plugin_id
;

/*
.print "\n ##Sample of gldemo ##"
SELECT *, (stop_iteration_record10.cpu_time - start_iteration_record9.cpu_time) AS elapsed
FROM (
	start_iteration_record9
	INNER JOIN stop_iteration_record10 USING (plugin_id, iteration, skip_iteration)
	LEFT JOIN plugin_start_record5 USING (plugin_id)
)
WHERE name = "gldemo"
LIMIT 10
;
*/

.print "\n## All plugins in iterations ##"
SELECT IFNULL(plugin_name, plugin_id) AS name, COUNT(threadloop_iteration_stop.cpu_time) AS iterations
FROM (
	threadloop_iteration_start
	INNER JOIN threadloop_iteration_stop USING (plugin_id, iteration_no)
	LEFT JOIN plugin_start USING (plugin_id)
)
GROUP BY name
;

.print "\n## All plugins in skip_iterations ##"
SELECT IFNULL(plugin_name, plugin_id) AS name, COUNT(threadloop_skip_stop.cpu_time) AS iterations
FROM (
	threadloop_skip_start
	INNER JOIN threadloop_skip_stop USING (plugin_id, iteration_no, skip_no)
	LEFT JOIN plugin_start USING (plugin_id)
)
GROUP BY name
;

/*
.print "\n## Sample of slam2 ##"
SELECT *, (stop_callback_record12.cpu_time - start_callback_record11.cpu_time) AS elapsed
FROM (
	start_callback_record11
	INNER JOIN stop_callback_record12 USING (plugin_id, serial_no)
	LEFT JOIN plugin_start_record5 USING (plugin_id)
)
WHERE name = "slam2"
LIMIT 10
;
*/

.print "\n## All plugins in callbacks ##"
SELECT IFNULL(plugin_name, plugin_id) AS name, COUNT(switchboard_callback_start.cpu_time) AS iterations
FROM (
	switchboard_callback_start
	INNER JOIN switchboard_callback_stop USING (plugin_id, serial_no)
	LEFT JOIN plugin_start USING (plugin_id)
)
GROUP BY name
;
