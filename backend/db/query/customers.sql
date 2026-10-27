-- Customers và Admins đã được gộp vào bảng users (phân biệt bằng cột role).

-- name: GetCustomerByID :one
SELECT user_id AS uid,
  first_name,
  last_name,
  email,
  phone_number,
  date_of_birth,
  gender,
  identification_number,
  passport_number,
  address,
  loyalty_points
FROM users
WHERE user_id = $1
  AND role = 'customer'
  AND deleted_at IS NULL;

-- name: GetCustomerByEmail :one
SELECT *
FROM users
WHERE email = $1
  AND role = 'customer'
  AND deleted_at IS NULL
LIMIT 1;

-- name: ListCustomers :many
SELECT *
FROM users
WHERE role = 'customer'
  AND deleted_at IS NULL
ORDER BY user_id DESC
LIMIT $1 OFFSET $2;

-- name: SoftDeleteCustomer :one
UPDATE users
SET deleted_at = NOW(),
  is_active = false,
  updated_at = NOW()
WHERE user_id = $1
  AND role = 'customer'
RETURNING user_id;
