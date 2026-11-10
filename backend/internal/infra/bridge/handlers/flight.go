package handlers

import (
	"context"
	"encoding/json"
	"time"

	"ltm-flight/internal/infra/bridge"
)

const (
	defaultPage  = 1
	defaultLimit = 10
	maxLimit     = 100
)

func registerFlight(r *bridge.Router, d Deps) {
	// FLIGHT_SEARCH: tim chuyen theo diem di/den va ngay di. Khach vang lai cung tim duoc.
	r.Handle(bridge.MsgFlightSearch, bridge.AuthNone, func(ctx context.Context, s *bridge.Session, raw json.RawMessage) (any, error) {
		var req struct {
			DepartureCity string `json:"departureCity"`
			ArrivalCity   string `json:"arrivalCity"`
			FlightDate    string `json:"flightDate"`
		}
		if err := decode(raw, &req); err != nil {
			return nil, err
		}
		if req.DepartureCity == "" || req.ArrivalCity == "" || req.FlightDate == "" {
			return nil, badRequest("departureCity, arrivalCity and flightDate are required")
		}
		date, err := parseFlightDate(req.FlightDate)
		if err != nil {
			return nil, badRequest("flightDate must be YYYY-MM-DD or RFC3339")
		}

		// Usecase hien nhan ngay dang chuoi "2006-01-02T15:04:05Z" nen chuan hoa truoc khi goi.
		flights, err := d.SearchFlights.Execute(ctx, req.DepartureCity, req.ArrivalCity, date.UTC().Format("2006-01-02T15:04:05Z"))
		if err != nil {
			return nil, err
		}
		return map[string]any{"message": "Flights retrieved successfully.", "data": flights}, nil
	})

	// FLIGHT_GET: chi tiet mot chuyen bay theo id.
	r.Handle(bridge.MsgFlightGet, bridge.AuthNone, func(ctx context.Context, s *bridge.Session, raw json.RawMessage) (any, error) {
		var req struct {
			FlightID flexInt64 `json:"flightId"`
		}
		if err := decode(raw, &req); err != nil {
			return nil, err
		}
		if req.FlightID <= 0 {
			return nil, badRequest("flightId is required")
		}
		return d.GetFlight.Execute(ctx, int64(req.FlightID))
	})

	// FLIGHT_LIST: danh sach chuyen bay goi y, co phan trang.
	r.Handle(bridge.MsgFlightList, bridge.AuthNone, func(ctx context.Context, s *bridge.Session, raw json.RawMessage) (any, error) {
		var req struct {
			Page  int `json:"page"`
			Limit int `json:"limit"`
		}
		if err := decode(raw, &req); err != nil {
			return nil, err
		}
		if req.Page < 1 {
			req.Page = defaultPage
		}
		if req.Limit < 1 {
			req.Limit = defaultLimit
		}
		if req.Limit > maxLimit {
			req.Limit = maxLimit
		}
		flights, err := d.ListFlights.Execute(ctx, req.Page, req.Limit)
		if err != nil {
			return nil, err
		}
		return map[string]any{"message": "Suggested flights retrieved successfully.", "data": flights}, nil
	})
}

// parseFlightDate nhan "2006-01-02" hoac RFC3339.
func parseFlightDate(s string) (time.Time, error) {
	if t, err := time.Parse("2006-01-02", s); err == nil {
		return t, nil
	}
	return time.Parse(time.RFC3339, s)
}
