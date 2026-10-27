-- name: CreateTicketChange :one
INSERT INTO ticketchanges (
  old_ticket_id,
  new_ticket_id,
  change_fee,
  fare_difference
) VALUES (
  $1, $2, $3, $4
) RETURNING *;

-- name: ListTicketChangesByTicket :many
SELECT * FROM ticketchanges
WHERE old_ticket_id = $1 OR new_ticket_id = $1
ORDER BY changed_at;
