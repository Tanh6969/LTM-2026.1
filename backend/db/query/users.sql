-- name: CreateUser :one
INSERT INTO users (
  email,
  hashed_password,
  first_name,
  last_name,
  role,
  phone_number,
  gender,
  date_of_birth,
  passport_number,
  identification_number,
  address
) VALUES (
  $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11
) RETURNING *;

-- name: GetUser :one
SELECT *
FROM users
WHERE user_id = $1;

-- name: GetAllUser :many
SELECT * FROM users;

-- name: ListUsers :many
SELECT *
FROM users
ORDER BY user_id
LIMIT $1
OFFSET $2;

-- name: GetUserByEmail :one
SELECT *
FROM users
WHERE email = $1;

-- name: DeleteUser :exec
DELETE FROM users
WHERE user_id = $1;

-- name: UpdateUserPassword :exec
UPDATE Users
SET
  hashed_password = $2,
  updated_at = now()
WHERE email = $1;

-- name: UpdateUser :exec
UPDATE users
SET first_name = $2,
    last_name = $3,
    phone_number = $4,
    gender = $5,
    date_of_birth = $6,
    passport_number = $7,
    identification_number = $8,
    address = $9,
    updated_at = NOW()
WHERE user_id = $1;

-- name: AddLoyaltyPoints :one
UPDATE users
SET loyalty_points = GREATEST(loyalty_points + $2, 0),
    updated_at = NOW()
WHERE user_id = $1
RETURNING loyalty_points;

-- name: DeactivateUser :exec
UPDATE Users
SET is_active = false,
    updated_at = now()
WHERE user_id = $1;
