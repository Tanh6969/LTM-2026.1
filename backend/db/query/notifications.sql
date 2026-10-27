-- name: CreateNotification :one
INSERT INTO notifications (
  user_id,
  booking_id,
  flight_id,
  type,
  title,
  message
) VALUES (
  $1, $2, $3, $4, $5, $6
) RETURNING *;

-- Thành viên: hộp thông báo của tôi.
-- name: ListNotificationsByUser :many
SELECT * FROM notifications
WHERE user_id = $1
ORDER BY created_at DESC
LIMIT $2 OFFSET $3;

-- Khách vãng lai: thông báo theo mã đặt chỗ (đã xác thực bằng PNR + email).
-- name: ListNotificationsByBooking :many
SELECT * FROM notifications
WHERE booking_id = $1
ORDER BY created_at DESC;

-- name: CountUnreadNotifications :one
SELECT COUNT(*)::int FROM notifications
WHERE user_id = $1 AND NOT is_read;

-- name: MarkNotificationRead :exec
UPDATE notifications
SET is_read = TRUE
WHERE notification_id = $1
  AND user_id = $2;

-- name: MarkAllNotificationsRead :exec
UPDATE notifications
SET is_read = TRUE
WHERE user_id = $1 AND NOT is_read;

-- name: MarkNotificationEmailSent :exec
UPDATE notifications
SET sent_email = TRUE
WHERE notification_id = $1;
