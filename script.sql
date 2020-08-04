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
ATTACH "metrics/switchboard_topic_stop.sqlite" AS p7;

/*
.print "\n## All registered plugins ##"
SELECT plugin_id, plugin_name
FROM plugin_start
ORDER BY plugin_id
;
*/

.print "\n## All plugins in iterations ##"
SELECT
	IFNULL(plugin_name, plugin_id) AS plugin_name,
	COUNT(threadloop_iteration_start.cpu_time) AS count_of_its,
	CAST(AVG(threadloop_iteration_stop.cpu_time - threadloop_iteration_start.cpu_time) AS INT) AS avg_time_per_it
FROM (
	threadloop_iteration_start
	INNER JOIN threadloop_iteration_stop USING (plugin_id, iteration_no)
	LEFT JOIN plugin_start USING (plugin_id)
)
GROUP BY plugin_id
;

.print "\n## All plugins in skip_iterations ##"
SELECT
	IFNULL(plugin_name, plugin_id) AS plugin_name,
	COUNT(threadloop_skip_start.cpu_time) AS count_of_its,
	CAST(AVG(threadloop_skip_stop.cpu_time - threadloop_skip_start.cpu_time) AS INT) AS avg_time_per_it
FROM (
	threadloop_skip_start
	INNER JOIN threadloop_skip_stop USING (plugin_id, iteration_no, skip_no)
	LEFT JOIN plugin_start USING (plugin_id)
)
GROUP BY plugin_id
;

.print "\n## All plugins in callbacks ##"
SELECT
	IFNULL(plugin_name, plugin_id) AS plugin_name,
	COUNT(switchboard_callback_start.cpu_time) AS count_of_its,
	CAST(AVG(switchboard_callback_stop.cpu_time - switchboard_callback_start.cpu_time) AS INT) AS avg_time_per_it
FROM (
	switchboard_callback_start
	INNER JOIN switchboard_callback_stop USING (plugin_id, serial_no)
	LEFT JOIN plugin_start USING (plugin_id)
)
GROUP BY plugin_id
;

.print "\n## Unfinished topics ##"
SELECT * FROM switchboard_topic_stop;
