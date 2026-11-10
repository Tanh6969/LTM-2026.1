package adapters

import (
	"context"
	"errors"

	"ltm-flight/internal/domain/entities"
)

var (
	ErrInvalidBooking  = errors.New("invalid booking data")
	ErrBookingNotFound = errors.New("booking not found")
)

type IBookingRepository interface {
	CreateBookingTx(ctx context.Context, booking entities.CreateBookingParams) (entities.Booking, []entities.Ticket, []entities.Ticket, error)
	GetBookingByID(ctx context.Context, bookingID int64) (entities.Booking, []entities.Ticket, []entities.Ticket, error)
}
