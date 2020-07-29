.header on
.mode column

ATTACH "metrics/component_start_record5.sqlite" AS p0;
ATTACH "metrics/start_iteration_record9.sqlite" AS p1;
ATTACH "metrics/stop_iteration_record10.sqlite" AS p2;
ATTACH "metrics/start_callback_record11.sqlite" AS p3;
ATTACH "metrics/stop_callback_record12.sqlite" AS p4;
ATTACH "metrics/start_skip_iteration_record7.sqlite" AS p5;
ATTACH "metrics/stop_skip_iteration_record8.sqlite" AS p6;

.print "\n## All registered plugins ##"
SELECT *
FROM component_start_record5
ORDER BY component_id
;

/*
.print "\n ##Sample of gldemo ##"
SELECT *, (stop_iteration_record10.cpu_time - start_iteration_record9.cpu_time) AS elapsed
FROM (
	start_iteration_record9
	INNER JOIN stop_iteration_record10 USING (component_id, iteration, skip_iteration)
	LEFT JOIN component_start_record5 USING (component_id)
)
WHERE name = "gldemo"
LIMIT 10
;
*/

.print "\n## All components in iterations ##"
SELECT IFNULL(name, component_id) AS name, COUNT(stop_iteration_record10.cpu_time) AS iterations
FROM (
	start_iteration_record9
	INNER JOIN stop_iteration_record10 USING (component_id, iteration, skip_iteration)
	LEFT JOIN component_start_record5 USING (component_id)
)
GROUP BY name
;

.print "\n## All components in skip_iterations ##"
SELECT IFNULL(name, component_id) AS name, COUNT(stop_skip_iteration_record8.cpu_time) AS iterations
FROM (
	start_skip_iteration_record7
	INNER JOIN stop_skip_iteration_record8 USING (component_id, iteration, skip_iteration)
	LEFT JOIN component_start_record5 USING (component_id)
)
GROUP BY name
;

/*
.print "\n## Sample of slam2 ##"
SELECT *, (stop_callback_record12.cpu_time - start_callback_record11.cpu_time) AS elapsed
FROM (
	start_callback_record11
	INNER JOIN stop_callback_record12 USING (component_id, serial_no)
	LEFT JOIN component_start_record5 USING (component_id)
)
WHERE name = "slam2"
LIMIT 10
;
*/

.print "\n## All components in callbacks ##"
SELECT IFNULL(name, component_id) AS name, COUNT(start_callback_record11.cpu_time) AS iterations
FROM (
	start_callback_record11
	INNER JOIN stop_callback_record12 USING (component_id, serial_no)
	LEFT JOIN component_start_record5 USING (component_id)
)
GROUP BY name
;
