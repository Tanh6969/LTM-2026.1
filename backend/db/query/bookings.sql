-- user_id NULL = khách vãng lai đặt vé bằng thông tin liên hệ.
-- name: CreateBooking :one
INSERT INTO bookings (
  user_id,
  contact_name,
  contact_email,
  contact_phone,
  pnr,
  trip_type,
  departure_flight_id,
  return_flight_id,
  status,
  total_amount,
  hold_expires_at
) VALUES (
  $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11
) RETURNING *;

-- name: GetBooking :one
SELECT * FROM bookings
WHERE booking_id = $1 LIMIT 1;

-- Tra cứu không cần đăng nhập: mã đặt chỗ + email liên hệ.
-- name: GetBookingByPNR :one
SELECT * FROM bookings
WHERE pnr = $1
  AND LOWER(contact_email) = LOWER($2)
LIMIT 1;

-- name: ListBookings :many
SELECT * FROM bookings
ORDER BY booking_id
LIMIT $1
OFFSET $2;

-- name: GetBookingHistoryByUID :many
SELECT booking_id
FROM bookings
WHERE user_id = $1
ORDER BY created_at DESC;

-- name: UpdateBookingStatus :one
UPDATE bookings
SET status = $2::booking_status,
    hold_expires_at = CASE WHEN $2::booking_status = 'pending' THEN hold_expires_at ELSE NULL END,
    updated_at = NOW()
WHERE booking_id = $1
RETURNING *;

-- name: UpdateBookingTotal :exec
UPDATE bookings
SET total_amount = $2,
    updated_at = NOW()
WHERE booking_id = $1;

-- Bộ lập lịch gọi: đơn chưa thanh toán quá hạn giữ chỗ -> expired. Trả về để nhả ghế.
-- name: ExpireUnpaidBookings :many
UPDATE bookings
SET status = 'expired',
    updated_at = NOW()
WHERE status = 'pending'
  AND hold_expires_at IS NOT NULL
  AND hold_expires_at < NOW()
RETURNING booking_id;

-- Danh sách đơn bị ảnh hưởng khi một chuyến bay trễ/hủy/đổi lịch (để tạo thông báo).
-- name: ListAffectedBookingsByFlight :many
SELECT DISTINCT b.booking_id,
  b.user_id,
  b.pnr,
  b.contact_name,
  b.contact_email,
  b.contact_phone
FROM bookings b
  JOIN tickets t ON t.booking_id = b.booking_id
WHERE t.flight_id = $1
  AND t.status IN ('Active', 'CheckedIn')
  AND b.status = 'confirmed';

-- name: DeleteBookings :exec
DELETE FROM bookings
WHERE booking_id = $1;

-- Khi xóa tài khoản: giữ lại đơn như đơn của khách vãng lai.
-- name: RemoveUserFromBookings :exec
UPDATE bookings
SET user_id = NULL,
    updated_at = NOW()
WHERE user_id = $1;
