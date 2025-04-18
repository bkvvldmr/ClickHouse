DROP TABLE IF EXISTS t1;
DROP TABLE IF EXISTS t2;

CREATE TABLE t1 (id Int) ENGINE = MergeTree ORDER BY tuple();
CREATE TABLE t2 (id Int) ENGINE = MergeTree ORDER BY tuple();

INSERT INTO t1 VALUES (1), (2);
INSERT INTO t2 VALUES (2), (3);

SELECT 70 = 10 * sum(t1.id) + sum(t2.id) AND count() == 4 FROM t1 JOIN t2 ON 1 = 1;
SELECT 70 = 10 * sum(t1.id) + sum(t2.id) AND count() == 4 FROM t1 JOIN t2 ON 1;
SELECT 70 = 10 * sum(t1.id) + sum(t2.id) AND count() == 4 FROM t1 JOIN t2 ON 2 = 2 AND 3 = 3;
SELECT 70 = 10 * sum(t1.id) + sum(t2.id) AND count() == 4 FROM t1 JOIN t2 ON toNullable(1);
SELECT 70 = 10 * sum(t1.id) + sum(t2.id) AND count() == 4 FROM t1 JOIN t2 ON toLowCardinality(1);
SELECT 70 = 10 * sum(t1.id) + sum(t2.id) AND count() == 4 FROM t1 JOIN t2 ON toLowCardinality(toNullable(1));
SELECT 70 = 10 * sum(t1.id) + sum(t2.id) AND count() == 4 FROM t1 JOIN t2 ON toNullable(toLowCardinality(1));

SELECT * FROM t1 JOIN t2 ON toUInt16(1); -- { serverError INVALID_JOIN_ON_EXPRESSION }
SELECT * FROM t1 JOIN t2 ON toInt8(1); -- { serverError INVALID_JOIN_ON_EXPRESSION }
SELECT * FROM t1 JOIN t2 ON 256; -- { serverError INVALID_JOIN_ON_EXPRESSION }
SELECT * FROM t1 JOIN t2 ON -1; -- { serverError INVALID_JOIN_ON_EXPRESSION }
SELECT * FROM t1 JOIN t2 ON toString(1); -- { serverError INVALID_JOIN_ON_EXPRESSION }

SELECT '- ON NULL -';

SELECT '- inner -';
SELECT * FROM t1 JOIN t2 ON NULL;
SELECT * FROM t1 JOIN t2 ON 0;
SELECT * FROM t1 JOIN t2 ON 1 = 2;
SELECT '- left -';
SELECT * FROM t1 LEFT JOIN t2 ON NULL ORDER BY t1.id, t2.id;
SELECT '- right -';
SELECT * FROM t1 RIGHT JOIN t2 ON NULL ORDER BY t1.id, t2.id;
SELECT '- full -';
SELECT * FROM t1 FULL JOIN t2 ON NULL ORDER BY t1.id, t2.id;

SELECT '- inner -';
SELECT * FROM t1 JOIN t2 ON NULL ORDER BY t1.id NULLS FIRST, t2.id SETTINGS join_use_nulls = 1;
SELECT '- left -';
SELECT * FROM t1 LEFT JOIN t2 ON NULL ORDER BY t1.id NULLS FIRST, t2.id SETTINGS join_use_nulls = 1;
SELECT '- right -';
SELECT * FROM t1 RIGHT JOIN t2 ON NULL ORDER BY t1.id NULLS FIRST, t2.id SETTINGS join_use_nulls = 1;
SELECT '- full -';
SELECT * FROM t1 FULL JOIN t2 ON NULL ORDER BY t1.id NULLS FIRST, t2.id SETTINGS join_use_nulls = 1;

-- in this cases in old analyzer we have AMBIGUOUS_COLUMN_NAME instead of INVALID_JOIN_ON_EXPRESSION
-- because there's some function in ON expression is not constant itself (result is constant)
SELECT * FROM t1 JOIN t2 ON 1 = 1 SETTINGS join_algorithm = 'full_sorting_merge'; -- { serverError AMBIGUOUS_COLUMN_NAME,NOT_IMPLEMENTED }
SELECT * FROM t1 JOIN t2 ON 1 = 1 SETTINGS join_algorithm = 'partial_merge'; -- { serverError AMBIGUOUS_COLUMN_NAME,NOT_IMPLEMENTED }
SELECT * FROM t1 JOIN t2 ON 1 = 1 SETTINGS join_algorithm = 'auto'; -- { serverError AMBIGUOUS_COLUMN_NAME,NOT_IMPLEMENTED }

SELECT * FROM t1 JOIN t2 ON NULL SETTINGS join_algorithm = 'full_sorting_merge'; -- { serverError INVALID_JOIN_ON_EXPRESSION,NOT_IMPLEMENTED }
SELECT * FROM t1 JOIN t2 ON NULL SETTINGS join_algorithm = 'partial_merge'; -- { serverError INVALID_JOIN_ON_EXPRESSION,NOT_IMPLEMENTED }
SELECT * FROM t1 LEFT JOIN t2 ON NULL SETTINGS join_algorithm = 'partial_merge'; -- { serverError INVALID_JOIN_ON_EXPRESSION,NOT_IMPLEMENTED }
SELECT * FROM t1 RIGHT JOIN t2 ON NULL SETTINGS join_algorithm = 'auto'; -- { serverError INVALID_JOIN_ON_EXPRESSION,NOT_IMPLEMENTED }
SELECT * FROM t1 FULL JOIN t2 ON NULL SETTINGS join_algorithm = 'partial_merge'; -- { serverError INVALID_JOIN_ON_EXPRESSION,NOT_IMPLEMENTED }

SET query_plan_use_new_logical_join_step = 1;

-- mixing of constant and non-constant expressions in ON is not allowed
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 1 == 1 SETTINGS enable_analyzer = 0; -- { serverError AMBIGUOUS_COLUMN_NAME }
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 1 == 1 SETTINGS enable_analyzer = 1;
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 1 == 2 SETTINGS enable_analyzer = 0; -- { serverError AMBIGUOUS_COLUMN_NAME }
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 1 == 2 SETTINGS enable_analyzer = 1;

SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 1 != 1 SETTINGS enable_analyzer = 0; -- { serverError INVALID_JOIN_ON_EXPRESSION }
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 1 != 1 SETTINGS enable_analyzer = 1;
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 'aaa'; -- { serverError INVALID_JOIN_ON_EXPRESSION,ILLEGAL_TYPE_OF_ARGUMENT }
SELECT * FROM t1 JOIN t2 ON 'aaa'; -- { serverError INVALID_JOIN_ON_EXPRESSION }

SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 0 SETTINGS enable_analyzer = 0; -- { serverError INVALID_JOIN_ON_EXPRESSION }
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 0 SETTINGS enable_analyzer = 1;
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 1 SETTINGS enable_analyzer = 0; -- { serverError INVALID_JOIN_ON_EXPRESSION }
SELECT * FROM t1 JOIN t2 ON t1.id = t2.id AND 1 SETTINGS enable_analyzer = 1;

-- { echoOn }
SELECT * FROM t1 LEFT JOIN t2 ON t1.id = t2.id AND 1 = 1 ORDER BY 1 SETTINGS enable_analyzer = 1;
SELECT * FROM t1 RIGHT JOIN t2 ON t1.id = t2.id AND 1 = 1 ORDER BY 1 SETTINGS enable_analyzer = 1;
SELECT * FROM t1 FULL JOIN t2 ON t1.id = t2.id AND 1 = 1 ORDER BY 2, 1 SETTINGS enable_analyzer = 1;

SELECT * FROM t1 LEFT JOIN t2 ON t1.id = t2.id AND 1 = 2 ORDER BY 1 SETTINGS enable_analyzer = 1;
SELECT * FROM t1 RIGHT JOIN t2 ON t1.id = t2.id AND 1 = 2 ORDER BY 2 SETTINGS enable_analyzer = 1;
SELECT * FROM t1 FULL JOIN t2 ON t1.id = t2.id AND 1 = 2 ORDER BY 2, 1 SETTINGS enable_analyzer = 1;

SELECT * FROM (SELECT 1 as a) as t1 INNER JOIN  ( SELECT ('b', 256) as b ) AS t2 ON NULL;
SELECT * FROM (SELECT 1 as a) as t1 LEFT JOIN  ( SELECT ('b', 256) as b ) AS t2 ON NULL;
SELECT * FROM (SELECT 1 as a) as t1 RIGHT JOIN  ( SELECT ('b', 256) as b ) AS t2 ON NULL;
SELECT * FROM (SELECT 1 as a) as t1 FULL JOIN  ( SELECT ('b', 256) as b ) AS t2 ON NULL ORDER BY 2;
SELECT * FROM (SELECT 1 as a) as t1 SEMI JOIN  ( SELECT ('b', 256) as b ) AS t2 ON NULL;
SELECT * FROM (SELECT 1 as a) as t1 ANTI JOIN  ( SELECT ('b', 256) as b ) AS t2 ON NULL ORDER BY 2;

-- { echoOff }

SELECT a + 1
FROM (SELECT 1 as x) as t1
LEFT JOIN ( SELECT 1 AS a ) AS t2
ON TRUE
SETTINGS enable_analyzer=1, join_use_nulls=1;

SELECT a + 1, x + 1, toTypeName(a), toTypeName(x)
FROM (SELECT 1 as x) as t1
LEFT JOIN ( SELECT sum(number) as a from numbers(3) GROUP BY NULL) AS t2
ON TRUE
SETTINGS enable_analyzer=1, join_use_nulls=1;

SELECT a + 1, x + 1, toTypeName(a), toTypeName(x)
FROM (SELECT 1 as x) as t1
RIGHT JOIN ( SELECT sum(number) as a from numbers(3) GROUP BY NULL) AS t2
ON TRUE
SETTINGS enable_analyzer=1, join_use_nulls=1;

SELECT a + 1, x + 1, toTypeName(a), toTypeName(x)
FROM (SELECT 1 as x) as t1
FULL JOIN ( SELECT sum(number) as a from numbers(3) GROUP BY NULL) AS t2
ON TRUE
SETTINGS enable_analyzer=1, join_use_nulls=1;

-- Join on constant with empty table fixed only with query_plan_use_new_logical_join_step
SET query_plan_use_new_logical_join_step = 1;
-- query_plan_use_new_logical_join_step disabled for parallel replicas
SET enable_parallel_replicas = 0;
SET join_use_nulls = 1;
SET enable_analyzer = 1;

CREATE TABLE empty_table (id Int) ENGINE = Memory;

SELECT * FROM t1 LEFT JOIN empty_table ON 1 = 1 ORDER BY ALL;
SELECT * FROM t1 FULL JOIN empty_table ON 1 = 1 ORDER BY ALL;
SELECT * FROM t1 LEFT JOIN empty_table ON 1 = 2 ORDER BY ALL;
SELECT * FROM t1 FULL JOIN empty_table ON 1 = 2 ORDER BY ALL;
SELECT * FROM empty_table RIGHT JOIN t1 ON 1 = 1 ORDER BY ALL;
SELECT * FROM empty_table FULL JOIN t1 ON 1 = 1 ORDER BY ALL;
SELECT * FROM empty_table RIGHT JOIN t1 ON 1 = 2 ORDER BY ALL;
SELECT * FROM empty_table FULL JOIN t1 ON 1 = 2 ORDER BY ALL;

SELECT '- empty -';
SELECT * FROM t1 JOIN empty_table ON 1 = 1;
SELECT * FROM t1 RIGHT JOIN empty_table ON 1 = 1;
SELECT * FROM t1 JOIN empty_table ON 1 = 2;
SELECT * FROM t1 RIGHT JOIN empty_table ON 1 = 2;
SELECT * FROM empty_table JOIN t1 ON 1 = 1;
SELECT * FROM empty_table LEFT JOIN t1 ON 1 = 1;
SELECT * FROM empty_table JOIN t1 ON 1 = 2;
SELECT * FROM empty_table LEFT JOIN t1 ON 1 = 2;
SELECT '- empty -';


DROP TABLE IF EXISTS t1;
DROP TABLE IF EXISTS t2;
DROP TABLE IF EXISTS empty_table;
