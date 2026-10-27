-- name: CreateSeat :one
INSERT INTO "seats" (
  flight_id,
  seat_code,
  class
) VALUES (
  $1, $2, $3
) RETURNING *;

-- name: GetSeat :one
SELECT * FROM "seats"
WHERE seat_id = $1 LIMIT 1;

-- name: GetAllSeats :many
SELECT * FROM "seats";

-- Sơ đồ ghế của một chuyến (ghế đang giữ nhưng đã quá hạn được xử lý bởi ReleaseExpiredSeatHolds).
-- name: ListSeatsWithFlightId :many
SELECT * FROM "seats"
WHERE flight_id = $1
ORDER BY seat_code;

-- name: CountOccupiedSeats :one
SELECT COUNT(*) FROM "seats"
WHERE flight_id = $1 AND status <> 'available';

-- name: CheckSeatAvailability :one
SELECT (
    status = 'available'
    OR (status = 'held' AND held_until < NOW())
  ) AS is_available
FROM "seats"
WHERE seat_code = $1 and flight_id = $2;

-- Giữ ghế tạm thời. Một câu lệnh nguyên tử: nếu ghế đã bị người khác giữ/đặt thì không có dòng nào
-- được trả về -> báo "ghế đã có người chọn". Chống 2 người cùng chọn một ghế.
-- name: HoldSeat :one
UPDATE seats
SET status = 'held',
    held_until = sqlc.arg(held_until),
    held_by_booking = sqlc.arg(booking_id)
WHERE flight_id = sqlc.arg(flight_id)
  AND seat_code = sqlc.arg(seat_code)
  AND (
    status = 'available'
    OR (status = 'held' AND held_until < NOW())
    OR (status = 'held' AND held_by_booking = sqlc.arg(booking_id))
  )
RETURNING *;

-- Chốt ghế sau khi thanh toán thành công.
-- name: BookSeat :one
UPDATE seats
SET status = 'booked',
    held_until = NULL,
    held_by_booking = NULL
WHERE flight_id = sqlc.arg(flight_id)
  AND seat_code = sqlc.arg(seat_code)
  AND (
    status = 'available'
    OR (status = 'held' AND held_by_booking = sqlc.arg(booking_id))
  )
RETURNING *;

-- name: ReleaseSeat :exec
UPDATE seats
SET status = 'available',
    held_until = NULL,
    held_by_booking = NULL
WHERE seat_id = $1;

-- Nhả mọi ghế đang giữ của một đơn (khi người dùng bỏ đơn hoặc đơn hết hạn).
-- name: ReleaseSeatsByBooking :many
UPDATE seats
SET status = 'available',
    held_until = NULL,
    held_by_booking = NULL
WHERE held_by_booking = $1
  AND status = 'held'
RETURNING seat_id, flight_id, seat_code;

-- Bộ lập lịch gọi: nhả ghế hết hạn giữ, trả về để broadcast SEAT_MAP_CHANGED.
-- name: ReleaseExpiredSeatHolds :many
UPDATE seats
SET status = 'available',
    held_until = NULL,
    held_by_booking = NULL
WHERE status = 'held'
  AND held_until < NOW()
RETURNING seat_id, flight_id, seat_code;

-- name: GetSeatByTicketID :one
SELECT s.seat_id, s.seat_code, s.class, s.status
FROM Seats s
JOIN Tickets t ON s.seat_id = t.seat_id
WHERE t.ticket_id = $1;

-- name: UpdateSeatStatus :exec
UPDATE Seats
SET status = $2::seat_status,
    held_until = CASE WHEN $2::seat_status = 'held' THEN held_until ELSE NULL END,
    held_by_booking = CASE WHEN $2::seat_status = 'held' THEN held_by_booking ELSE NULL END
WHERE seat_id = (SELECT seat_id FROM Tickets WHERE ticket_id = $1);
