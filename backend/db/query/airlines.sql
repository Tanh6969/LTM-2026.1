-- name: CreateAirline :one
INSERT INTO airlines (code, name, logo_url)
VALUES ($1, $2, $3)
RETURNING *;

-- name: GetAirline :one
SELECT * FROM airlines
WHERE airline_id = $1
LIMIT 1;

-- name: ListAirlines :many
SELECT * FROM airlines
ORDER BY name;
