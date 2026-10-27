-- name: CreateFlightStatusHistory :one
INSERT INTO flightstatushistory (
    flight_id,
    old_status,
    new_status,
    old_departure_time,
    new_departure_time,
    reason,
    changed_by
  )
VALUES ($1, $2, $3, $4, $5, $6, $7)
RETURNING *;

-- name: ListFlightStatusHistory :many
SELECT *
FROM flightstatushistory
WHERE flight_id = $1
ORDER BY changed_at DESC
LIMIT $2 OFFSET $3;
