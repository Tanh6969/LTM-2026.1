-- Admin là user có role = 'admin'.

-- name: GetAdmin :one
SELECT *
FROM users
WHERE user_id = $1
  AND role = 'admin'
LIMIT 1;

-- name: GetAdminByEmail :one
SELECT *
FROM users
WHERE email = $1
  AND role = 'admin'
LIMIT 1;

-- name: ListAdmins :many
SELECT *
FROM users
WHERE role = 'admin'
ORDER BY user_id
LIMIT $1 OFFSET $2;

-- name: SetUserRole :one
UPDATE users
SET role = $2,
  updated_at = NOW()
WHERE user_id = $1
RETURNING *;

-- name: IsAdmin :one
SELECT EXISTS (
    SELECT 1
    FROM users
    WHERE user_id = $1
      AND role = 'admin'
      AND is_active
  ) AS is_admin;
