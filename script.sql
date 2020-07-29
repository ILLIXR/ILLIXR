.header on
.mode column

ATTACH "metrics/component_start_record5.sqlite" AS p0;
ATTACH "metrics/start_iteration_record9.sqlite" AS p1;
ATTACH "metrics/stop_iteration_record10.sqlite" AS p2;
ATTACH "metrics/start_callback_record11.sqlite" AS p3;
ATTACH "metrics/stop_callback_record12.sqlite" AS p4;

-- Sample of gldemo from "iterations"

SELECT *, (stop_iteration_record10.cpu_time - start_iteration_record9.cpu_time) AS elapsed
FROM (
	start_iteration_record9
	INNER JOIN stop_iteration_record10 USING (component_id, iteration, skip_iteration)
	INNER JOIN component_start_record5 USING (component_id)
)
WHERE name = "gldemo"
LIMIT 10;

-- All components which spend time in "iterations"

SELECT name, COUNT(stop_iteration_record10.cpu_time) AS iterations
FROM (
	start_iteration_record9
	INNER JOIN stop_iteration_record10 USING (component_id, iteration, skip_iteration)
	INNER JOIN component_start_record5 USING (component_id)
)
GROUP BY name
;

-- Sample of slam2 from callbacks

SELECT *, (stop_callback_record12.cpu_time - start_callback_record11.cpu_time) AS elapsed
FROM (
	start_callback_record11
	INNER JOIN stop_callback_record12 USING (component_id, serial_no)
	INNER JOIN component_start_record5 USING (component_id)
)
WHERE name = "slam2"
LIMIT 10;

-- All components which spend time in callbacks

SELECT name, COUNT(start_callback_record11.cpu_time) AS iterations
FROM (
	start_callback_record11
	INNER JOIN stop_callback_record12 USING (component_id, serial_no)
	INNER JOIN component_start_record5 USING (component_id)
)
GROUP BY name
;
