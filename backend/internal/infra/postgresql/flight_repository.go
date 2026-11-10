package postgresql

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgtype"
	db "ltm-flight/db/sqlc"
	"ltm-flight/internal/domain/adapters"
	"ltm-flight/internal/domain/entities"
)

// So hang ghe thuong gia toi da o dau may bay; phan con lai la pho thong.
const businessRows = 4

type FlightRepositoryPostgres struct {
	store db.Store
}

func NewFlightRepositoryPostgres(store *db.Store) *FlightRepositoryPostgres {
	return &FlightRepositoryPostgres{store: *store}
}

// CreateFlight tao chuyen bay va hai muc gia (pho thong = BasePrice, thuong gia = 2 x BasePrice).
// Viec tao so do ghe (Seats) thuoc giai doan 6.
func (r *FlightRepositoryPostgres) CreateFlight(ctx context.Context, flight entities.Flight) (entities.Flight, error) {
	if flight.AirlineID == 0 {
		return entities.Flight{}, fmt.Errorf("airline_id is required")
	}
	dbFlight, err := r.store.CreateFlight(ctx, db.CreateFlightParams{
		FlightNumber:     flight.FlightNumber,
		AirlineID:        flight.AirlineID,
		AircraftType:     pgtype.Text{String: flight.AircraftType, Valid: true},
		DepartureCity:    pgtype.Text{String: flight.DepartureCity, Valid: true},
		ArrivalCity:      pgtype.Text{String: flight.ArrivalCity, Valid: true},
		DepartureAirport: pgtype.Text{String: flight.DepartureAirport, Valid: true},
		ArrivalAirport:   pgtype.Text{String: flight.ArrivalAirport, Valid: true},
		DepartureTime:    flight.DepartureTime,
		ArrivalTime:      flight.ArrivalTime,
		TotalSeatsRow:    flight.TotalSeatsRow,
		TotalSeatsColumn: flight.TotalSeatsColumn,
		Status:           db.FlightStatus(flight.Status),
	})
	if err != nil {
		return entities.Flight{}, err
	}

	rows := dbFlight.TotalSeatsRow
	cols := dbFlight.TotalSeatsColumn
	bizRows := min(rows, businessRows)
	fares := []db.UpsertFlightFareParams{
		{FlightID: dbFlight.FlightID, Class: db.FlightClassEconomy, Price: flight.BasePrice, SeatsTotal: (rows - bizRows) * cols},
		{FlightID: dbFlight.FlightID, Class: db.FlightClassBusiness, Price: flight.BasePrice * 2, SeatsTotal: bizRows * cols},
	}
	for _, fare := range fares {
		if _, err := r.store.UpsertFlightFare(ctx, fare); err != nil {
			return entities.Flight{}, err
		}
	}

	return entities.Flight{
		FlightID:         dbFlight.FlightID,
		FlightNumber:     dbFlight.FlightNumber,
		AirlineID:        dbFlight.AirlineID,
		AircraftType:     dbFlight.AircraftType.String,
		DepartureCity:    dbFlight.DepartureCity.String,
		ArrivalCity:      dbFlight.ArrivalCity.String,
		DepartureAirport: dbFlight.DepartureAirport.String,
		ArrivalAirport:   dbFlight.ArrivalAirport.String,
		DepartureTime:    dbFlight.DepartureTime,
		ArrivalTime:      dbFlight.ArrivalTime,
		BasePrice:        flight.BasePrice,
		TotalSeatsRow:    rows,
		TotalSeatsColumn: cols,
		Status:           entities.FlightStatus(dbFlight.Status),
	}, nil
}

func (r *FlightRepositoryPostgres) GetFlightByID(ctx context.Context, flightID int64) (*entities.Flight, error) {
	row, err := r.store.GetFlight(ctx, flightID)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, adapters.ErrFlightNotFound
	}
	if err != nil {
		return nil, err
	}
	return &entities.Flight{
		FlightID:         row.FlightID,
		FlightNumber:     row.FlightNumber,
		Airline:          row.AirlineName,
		AirlineID:        row.AirlineID,
		AirlineCode:      row.AirlineCode,
		AircraftType:     row.AircraftType.String,
		DepartureCity:    row.DepartureCity.String,
		ArrivalCity:      row.ArrivalCity.String,
		DepartureAirport: row.DepartureAirport.String,
		ArrivalAirport:   row.ArrivalAirport.String,
		DepartureTime:    row.DepartureTime,
		ArrivalTime:      row.ArrivalTime,
		BasePrice:        row.BasePrice,
		TotalSeatsRow:    row.TotalSeatsRow,
		TotalSeatsColumn: row.TotalSeatsColumn,
		Status:           entities.FlightStatus(row.Status),
	}, nil
}

func (r *FlightRepositoryPostgres) UpdateFlightTimes(ctx context.Context, flightID int64, departureTime, arrivalTime time.Time) (*entities.Flight, error) {
	row, err := r.store.UpdateFlightTimes(ctx, db.UpdateFlightTimesParams{
		FlightID:      flightID,
		DepartureTime: departureTime,
		ArrivalTime:   arrivalTime,
	})
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, adapters.ErrFlightNotFound
	}
	if err != nil {
		return nil, err
	}
	return &entities.Flight{
		FlightID:      row.FlightID,
		DepartureTime: row.DepartureTime,
		ArrivalTime:   row.ArrivalTime,
	}, nil
}

func (r *FlightRepositoryPostgres) GetAllFlights(ctx context.Context) ([]entities.Flight, error) {
	rows, err := r.store.GetAllFlights(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get all flights: %w", err)
	}

	flights := make([]entities.Flight, 0, len(rows))
	for _, row := range rows {
		flights = append(flights, entities.Flight{
			FlightID:      row.FlightID,
			FlightNumber:  row.FlightNumber,
			Airline:       row.AirlineName,
			AircraftType:  row.AircraftType.String,
			DepartureCity: row.DepartureCity.String,
			ArrivalCity:   row.ArrivalCity.String,
			DepartureTime: row.DepartureTime,
			ArrivalTime:   row.ArrivalTime,
			BasePrice:     row.BasePrice,
			Status:        entities.FlightStatus(row.Status),
		})
	}
	return flights, nil
}

func (r *FlightRepositoryPostgres) DeleteFlightByID(ctx context.Context, flightID int64) error {
	deletedID, err := r.store.DeleteFlight(ctx, flightID)
	if errors.Is(err, pgx.ErrNoRows) || (err == nil && deletedID == 0) {
		return adapters.ErrFlightNotFound
	}
	if err != nil {
		return fmt.Errorf("failed to delete flight: %w", err)
	}
	return nil
}

// SearchFlights tim chuyen theo diem di/den va ngay di, hang pho thong, 1 hanh khach, moi hang bay,
// sap xep theo gio khoi hanh. Bo loc hang ghe/hang bay/sap xep theo gia o giai doan 5.
func (r *FlightRepositoryPostgres) SearchFlights(ctx context.Context, departureCity, arrivalCity string, flightDate time.Time) ([]entities.Flight, error) {
	rows, err := r.store.SearchFlights(ctx, db.SearchFlightsParams{
		DepartureCity: pgtype.Text{String: departureCity, Valid: true},
		ArrivalCity:   pgtype.Text{String: arrivalCity, Valid: true},
		DepartureDate: flightDate,
		Class:         db.FlightClassEconomy,
		Passengers:    1,
		SortBy:        "time",
	})
	if err != nil {
		return nil, fmt.Errorf("failed to search flights: %w", err)
	}
	if len(rows) == 0 {
		return nil, adapters.ErrNoFlightsFound
	}

	flights := make([]entities.Flight, 0, len(rows))
	for _, row := range rows {
		flights = append(flights, entities.Flight{
			FlightID:         row.FlightID,
			FlightNumber:     row.FlightNumber,
			Airline:          row.AirlineName,
			AirlineID:        row.AirlineID,
			AirlineCode:      row.AirlineCode,
			DepartureCity:    row.DepartureCity.String,
			ArrivalCity:      row.ArrivalCity.String,
			DepartureTime:    row.DepartureTime,
			ArrivalTime:      row.ArrivalTime,
			DepartureAirport: row.DepartureAirport.String,
			ArrivalAirport:   row.ArrivalAirport.String,
			AircraftType:     row.AircraftType.String,
			Status:           entities.FlightStatus(row.Status),
			BasePrice:        row.Price,
			Class:            entities.FlightClass(row.Class),
			DurationSeconds:  row.DurationSeconds,
			SeatsLeft:        row.SeatsLeft,
		})
	}
	return flights, nil
}

func (r *FlightRepositoryPostgres) ListFlights(ctx context.Context, offset int, limit int) ([]entities.Flight, error) {
	rows, err := r.store.ListFlights(ctx, db.ListFlightsParams{
		Limit:  int32(limit),
		Offset: int32(offset),
	})
	if err != nil {
		return nil, err
	}
	if len(rows) == 0 {
		return nil, adapters.ErrNoSuggestedFlights
	}

	flights := make([]entities.Flight, 0, len(rows))
	for _, row := range rows {
		flights = append(flights, entities.Flight{
			FlightID:         row.FlightID,
			FlightNumber:     row.FlightNumber,
			Airline:          row.AirlineName,
			AirlineCode:      row.AirlineCode,
			DepartureCity:    row.DepartureCity.String,
			ArrivalCity:      row.ArrivalCity.String,
			DepartureTime:    row.DepartureTime,
			ArrivalTime:      row.ArrivalTime,
			DepartureAirport: row.DepartureAirport.String,
			ArrivalAirport:   row.ArrivalAirport.String,
			AircraftType:     row.AircraftType.String,
			Status:           entities.FlightStatus(row.Status),
			BasePrice:        row.BasePrice,
		})
	}
	return flights, nil
}
