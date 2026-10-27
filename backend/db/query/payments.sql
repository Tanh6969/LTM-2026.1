-- name: CreatePayment :one
INSERT INTO payments (
  booking_id,
  method,
  provider,
  provider_ref,
  amount,
  status
) VALUES (
  $1, $2, $3, $4, $5, $6
) RETURNING *;

-- name: GetPaymentByProviderRef :one
SELECT * FROM payments
WHERE provider_ref = $1
LIMIT 1;

-- name: ListPaymentsByBooking :many
SELECT * FROM payments
WHERE booking_id = $1
ORDER BY created_at;

-- name: UpdatePaymentStatus :one
UPDATE payments
SET status = $2
WHERE payment_id = $1
RETURNING *;

-- Doanh thu theo ngày (đã trừ hoàn tiền: amount âm). Dùng cho dashboard admin.
-- name: RevenueByDay :many
SELECT (created_at AT TIME ZONE 'Asia/Ho_Chi_Minh')::date AS day,
  SUM(amount)::bigint AS revenue,
  COUNT(*) FILTER (WHERE amount > 0)::int AS payments
FROM payments
WHERE status IN ('succeeded', 'refunded')
  AND created_at >= sqlc.arg(from_time)
  AND created_at < sqlc.arg(to_time)
GROUP BY 1
ORDER BY 1;

-- Doanh thu theo hãng.
-- name: RevenueByAirline :many
SELECT a.name AS airline_name,
  SUM(t.price)::bigint AS revenue,
  COUNT(*)::int AS tickets
FROM tickets t
  JOIN flights f ON f.flight_id = t.flight_id
  JOIN airlines a ON a.airline_id = f.airline_id
  JOIN bookings b ON b.booking_id = t.booking_id
WHERE b.status = 'confirmed'
  AND t.status IN ('Active', 'CheckedIn')
GROUP BY a.name
ORDER BY revenue DESC;

-- Số liệu tổng quan cho dashboard (thay cho số cố định trong /api/statistic cũ).
-- name: GetDashboardTotals :one
SELECT
  (SELECT COUNT(*) FROM flights)::int AS flights,
  (SELECT COUNT(*) FROM tickets WHERE status IN ('Active', 'CheckedIn'))::int AS tickets,
  (SELECT COALESCE(SUM(amount), 0) FROM payments WHERE status IN ('succeeded', 'refunded'))::bigint AS revenue;
