-- name: UpsertFlightFare :one
INSERT INTO flightfares (flight_id, class, price, seats_total)
VALUES ($1, $2, $3, $4)
ON CONFLICT (flight_id, class) DO UPDATE
SET price = EXCLUDED.price,
  seats_total = EXCLUDED.seats_total
RETURNING *;

-- name: ListFlightFares :many
SELECT ff.flight_id,
  ff.class,
  ff.price,
  ff.seats_total,
  (
    ff.seats_total - (
      SELECT COUNT(*)
      FROM tickets t
      WHERE t.flight_id = ff.flight_id
        AND t.flight_class = ff.class
        AND t.status IN ('Active', 'CheckedIn')
    )
  )::int AS seats_left
FROM flightfares ff
WHERE ff.flight_id = $1
ORDER BY ff.price;
