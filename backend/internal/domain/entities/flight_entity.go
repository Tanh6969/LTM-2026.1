package entities

import "time"

type FlightStatus string

const (
	FlightOnTimeStatus   FlightStatus = "On Time"
	FlightDelayedStatus  FlightStatus = "Delayed"
	FlightCanceledStatus FlightStatus = "Cancelled"
	FlightBoardingStatus FlightStatus = "Boarding"
	FlightTakeoffStatus  FlightStatus = "Takeoff"
	FlightLandingStatus  FlightStatus = "Landing"
	FlightLandedStatus   FlightStatus = "Landed"
)

type Flight struct {
	FlightID         int64        `json:"flight_id"`
	FlightNumber     string       `json:"flight_number"`
	Airline          string       `json:"airline"`
	AirlineID        int64        `json:"airline_id"`   // khoa ngoai toi bang Airlines
	AirlineCode      string       `json:"airline_code"` // ma hang, vd VN, VJ
	AircraftType     string       `json:"aircraft_type"`
	DepartureCity    string       `json:"departure_city"`
	ArrivalCity      string       `json:"arrival_city"`
	DepartureAirport string       `json:"departure_airport"`
	ArrivalAirport   string       `json:"arrival_airport"`
	DepartureTime    time.Time    `json:"departure_time"`
	ArrivalTime      time.Time    `json:"arrival_time"`
	BasePrice        int32        `json:"base_price"` // gia thap nhat trong cac hang ghe; khi tim kiem la gia cua hang duoc chon
	TotalSeatsRow    int32        `json:"total_seats_row"`
	TotalSeatsColumn int32        `json:"total_seats_column"`
	Status           FlightStatus `json:"status"`

	// Chi co gia tri khi lay tu ket qua tim kiem (SearchFlights).
	Class           FlightClass `json:"class,omitempty"`
	DurationSeconds int64       `json:"duration_seconds,omitempty"`
	SeatsLeft       int32       `json:"seats_left,omitempty"`
}

type CreateFlightParams struct {
	FlightID         int64  `json:"flight_id"`
	FlightNumber     string `json:"flight_number"`
	AircraftType     string `json:"aircraft_type"`
	DepartureCity    string `json:"departure_city"`
	ArrivalCity      string `json:"arrival_city"`
	DepartureAirport string `json:"departure_airport"`
	ArrivalAirport   string `json:"arrival_airport"`
	DepartureTime    string `json:"departure_time"`
	ArrivalTime      string `json:"arrival_time"`
	BasePrice        int32  `json:"base_price"`
	Status           string `json:"status"`
}
