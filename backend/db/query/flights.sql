-- name: CreateFlight :one
INSERT INTO flights (
    flight_number,
    airline_id,
    aircraft_type,
    departure_city,
    arrival_city,
    departure_airport,
    arrival_airport,
    departure_time,
    arrival_time,
    total_seats_row,
    total_seats_column,
    status
  )
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)
RETURNING *;

-- name: GetFlight :one
SELECT f.*,
  a.code AS airline_code,
  a.name AS airline_name,
  COALESCE((SELECT MIN(price) FROM flightfares WHERE flight_id = f.flight_id), 0)::int AS base_price
FROM flights f
  JOIN airlines a ON a.airline_id = f.airline_id
WHERE f.flight_id = $1
LIMIT 1;

-- name: GetAllFlights :many
SELECT f.flight_id,
  f.flight_number,
  f.aircraft_type,
  f.departure_city,
  f.arrival_city,
  f.departure_time,
  f.arrival_time,
  f.status,
  a.name AS airline_name,
  COALESCE((SELECT MIN(price) FROM flightfares WHERE flight_id = f.flight_id), 0)::int AS base_price
FROM flights f
  JOIN airlines a ON a.airline_id = f.airline_id;

-- name: ListFlights :many
SELECT f.flight_id,
  f.flight_number,
  a.code AS airline_code,
  a.name AS airline_name,
  f.departure_city,
  f.arrival_city,
  f.departure_time,
  f.arrival_time,
  f.departure_airport,
  f.arrival_airport,
  f.aircraft_type,
  f.status,
  COALESCE((SELECT MIN(price) FROM flightfares WHERE flight_id = f.flight_id), 0)::int AS base_price
FROM flights f
  JOIN airlines a ON a.airline_id = f.airline_id
ORDER BY f.flight_id
LIMIT $1 OFFSET $2;

-- name: GetFlightsByStatus :one
SELECT status
FROM flights
WHERE flight_id = $1
LIMIT 1;

-- name: DeleteFlight :one
DELETE FROM flights
WHERE flight_id = $1
RETURNING flight_id;

-- name: UpdateFlightTimes :one
UPDATE Flights
SET departure_time = $2,
  arrival_time = $3
WHERE flight_id = $1
RETURNING flight_id,
  departure_time,
  arrival_time;

-- Admin báo sự cố: trễ / hủy / đổi lịch. Giờ mới để NULL nếu không đổi giờ.
-- name: DisruptFlight :one
UPDATE flights
SET status = sqlc.arg(status),
  departure_time = COALESCE(sqlc.narg(new_departure_time), departure_time),
  arrival_time = COALESCE(sqlc.narg(new_arrival_time), arrival_time)
WHERE flight_id = sqlc.arg(flight_id)
RETURNING *;

-- Bộ lập lịch gọi mỗi phút: tự chuyển -> Boarding -> Takeoff -> Landed theo giờ bay.
-- Chuyến Cancelled/Landed không bị đụng tới. Mỗi lần đổi đều ghi FlightStatusHistory (changed_by NULL).
-- name: AdvanceFlightStatuses :many
WITH target AS (
  SELECT flight_id,
    status AS old_status,
    CASE
      WHEN now() >= arrival_time THEN 'Landed'::flight_status
      WHEN now() >= departure_time THEN 'Takeoff'::flight_status
      WHEN now() >= departure_time - interval '40 minutes' THEN 'Boarding'::flight_status
      ELSE status
    END AS new_status
  FROM flights
  WHERE status NOT IN ('Cancelled', 'Landed')
),
changed AS (
  UPDATE flights f
  SET status = t.new_status
  FROM target t
  WHERE f.flight_id = t.flight_id
    AND t.new_status <> t.old_status
  RETURNING f.flight_id, t.old_status, f.status AS new_status
)
INSERT INTO flightstatushistory (flight_id, old_status, new_status, reason)
SELECT flight_id, old_status, new_status, 'auto'
FROM changed
RETURNING flight_id, old_status, new_status;

-- Tìm kiếm theo điểm đi/đến, ngày đi, hạng ghế, số hành khách. Mỗi dòng là 1 chuyến ở 1 hạng.
-- Trả về mọi hãng nên dùng luôn cho so sánh giá/thời gian bay. airline_id NULL = mọi hãng.
-- sort_by: 'price' | 'duration' | mặc định theo giờ khởi hành.
-- name: SearchFlights :many
SELECT f.flight_id,
  f.flight_number,
  a.airline_id,
  a.code AS airline_code,
  a.name AS airline_name,
  a.logo_url,
  f.departure_city,
  f.arrival_city,
  f.departure_airport,
  f.arrival_airport,
  f.departure_time,
  f.arrival_time,
  f.aircraft_type,
  f.status,
  fa.class,
  fa.price,
  EXTRACT(EPOCH FROM (f.arrival_time - f.departure_time))::bigint AS duration_seconds,
  (
    fa.seats_total - (
      SELECT COUNT(*)
      FROM tickets t
      WHERE t.flight_id = f.flight_id
        AND t.flight_class = fa.class
        AND t.status IN ('Active', 'CheckedIn')
    )
  )::int AS seats_left
FROM flights f
  JOIN airlines a ON a.airline_id = f.airline_id
  JOIN flightfares fa ON fa.flight_id = f.flight_id
WHERE f.departure_city = sqlc.arg(departure_city)
  AND f.arrival_city = sqlc.arg(arrival_city)
  AND (f.departure_time AT TIME ZONE 'Asia/Ho_Chi_Minh')::date = sqlc.arg(departure_date)::date
  AND fa.class = sqlc.arg(class)
  AND f.status <> 'Cancelled'
  AND (sqlc.narg(airline_id)::bigint IS NULL OR f.airline_id = sqlc.narg(airline_id))
  AND fa.seats_total - (
    SELECT COUNT(*)
    FROM tickets t
    WHERE t.flight_id = f.flight_id
      AND t.flight_class = fa.class
      AND t.status IN ('Active', 'CheckedIn')
  ) >= sqlc.arg(passengers)::int
ORDER BY
  CASE WHEN sqlc.arg(sort_by)::text = 'price' THEN fa.price END ASC,
  CASE WHEN sqlc.arg(sort_by)::text = 'duration' THEN EXTRACT(EPOCH FROM (f.arrival_time - f.departure_time)) END ASC,
  f.departure_time ASC;
