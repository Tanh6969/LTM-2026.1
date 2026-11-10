package booking

import (
	"context"
	"errors"
	"strconv"

	"ltm-flight/internal/domain/adapters"
	"ltm-flight/internal/domain/entities"
	"ltm-flight/internal/infra/api/dto"
	"ltm-flight/internal/infra/api/mappers"
)

type ICreateBookingUseCase interface {
	Execute(ctx context.Context, booking dto.CreateBookingRequest, email string) (dto.CreateBookingResponse, error)
}

// CreateBookingUseCase là bản của hệ thống cũ (1 hãng, bắt buộc có tài khoản) và sẽ được viết lại ở giai đoạn 6
// theo schema mới (đặt vé cho khách vãng lai, PNR, giữ ghế). Việc gửi vé điện tử qua email không còn nằm ở Go:
// netcore (C) gửi email qua SMTP ở giai đoạn 7.
type CreateBookingUseCase struct {
	bookingRepository adapters.IBookingRepository
	flightRepository  adapters.IFlightRepository
}

func NewCreateBookingUseCase(bookingRepository adapters.IBookingRepository, flightRepository adapters.IFlightRepository) ICreateBookingUseCase {
	return &CreateBookingUseCase{
		bookingRepository: bookingRepository,
		flightRepository:  flightRepository,
	}
}

func (u *CreateBookingUseCase) Execute(ctx context.Context, booking dto.CreateBookingRequest, email string) (dto.CreateBookingResponse, error) {
	// Kiểm tra sự tồn tại của chuyến bay đi
	departureFlightID, err := strconv.ParseInt(booking.DepartureFlightID, 10, 64)
	if err != nil {
		return dto.CreateBookingResponse{}, adapters.ErrFlightNotFound
	}
	departureFlight, err := u.flightRepository.GetFlightByID(ctx, departureFlightID)
	if err != nil {
		if errors.Is(err, adapters.ErrFlightNotFound) {
			return dto.CreateBookingResponse{}, adapters.ErrFlightNotFound
		}
		return dto.CreateBookingResponse{}, err
	}

	// Kiểm tra sự tồn tại của chuyến bay về (nếu là roundTrip)
	var returnFlight *entities.Flight
	if booking.TripType == "roundTrip" {
		returnFlightID, err := strconv.ParseInt(booking.ReturnFlightID, 10, 64)
		if err != nil {
			return dto.CreateBookingResponse{}, err
		}
		returnFlight, err = u.flightRepository.GetFlightByID(ctx, returnFlightID)
		if err != nil {
			if errors.Is(err, adapters.ErrFlightNotFound) {
				return dto.CreateBookingResponse{}, adapters.ErrFlightNotFound
			}
			return dto.CreateBookingResponse{}, err
		}
	}

	// Tạo booking trong repository
	params := mappers.ToCreateBookingParams(booking, *departureFlight, returnFlight, email)
	arg := entities.CreateBookingParams{
		Email:                   params.Email,
		DepartureCity:           params.DepartureCity,
		ArrivalCity:             params.ArrivalCity,
		DepartureFlightID:       params.DepartureFlightID,
		ReturnFlightID:          params.ReturnFlightID,
		TripType:                params.TripType,
		DepartureTicketDataList: params.DepartureTicketDataList,
		ReturnTicketDataList:    params.ReturnTicketDataList,
	}
	createdBooking, departureTickets, returnTickets, err := u.bookingRepository.CreateBookingTx(ctx, arg)
	if err != nil {
		return dto.CreateBookingResponse{}, err
	}

	// Map kết quả sang DTO
	return mappers.ToCreateBookingResponse(createdBooking, departureTickets, returnTickets), nil
}
