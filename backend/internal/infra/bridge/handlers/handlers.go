// Package handlers noi tung loai thong diep cua kenh socket voi usecase trong domain.
// Moi handler lam ba viec: giai ma payload JSON, goi dung mot usecase, tra ket qua (hoac loi).
// Logic nghiep vu nam o usecase; cac handler nay thay cho tang Gin trong Qairlines.
package handlers

import (
	"bytes"
	"encoding/json"
	"strconv"

	"ltm-flight/internal/domain/usecases"
	"ltm-flight/internal/domain/usecases/auth"
	"ltm-flight/internal/domain/usecases/customer"
	"ltm-flight/internal/domain/usecases/flight"
	"ltm-flight/internal/infra/bridge"
	"ltm-flight/pkg/token"
)

// Deps gom cac usecase ma handler can. Them nhom nghiep vu moi (dat ve, thanh toan...) thi them truong o day.
type Deps struct {
	Health         usecases.IHealthUseCase
	Login          auth.ILoginUseCase
	CreateCustomer customer.ICreateCustomerUseCase
	GetFlight      flight.IGetFlightUseCase
	SearchFlights  flight.ISearchFlightsUseCase
	ListFlights    flight.IListFlightsUseCase
	TokenMaker     token.Maker
}

// Register dang ky moi handler len router.
func Register(r *bridge.Router, d Deps) {
	registerSystem(r, d)
	registerAuth(r, d)
	registerFlight(r, d)
}

// decode giai ma payload vao dst. Payload rong duoc coi la {} de cac thong diep khong tham so van chay.
func decode(raw json.RawMessage, dst any) error {
	if len(bytes.TrimSpace(raw)) == 0 {
		raw = json.RawMessage("{}")
	}
	if err := json.Unmarshal(raw, dst); err != nil {
		return bridge.NewError(bridge.StatusBadRequest, "invalid request payload")
	}
	return nil
}

func badRequest(msg string) error {
	return bridge.NewError(bridge.StatusBadRequest, msg)
}

// flexInt64 nhan ca so (12) lan chuoi ("12") trong JSON vi FE hien dang gui id dang chuoi.
type flexInt64 int64

func (n *flexInt64) UnmarshalJSON(b []byte) error {
	s := string(bytes.Trim(bytes.TrimSpace(b), `"`))
	v, err := strconv.ParseInt(s, 10, 64)
	if err != nil {
		return err
	}
	*n = flexInt64(v)
	return nil
}
