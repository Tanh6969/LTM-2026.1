package bridge

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"

	"github.com/jackc/pgx/v5"
	"ltm-flight/internal/domain/adapters"
	appErrors "ltm-flight/pkg/errors"
)

// AuthLevel la muc dang nhap toi thieu de goi mot handler.
type AuthLevel int

const (
	AuthNone  AuthLevel = iota // khach vang lai cung goi duoc
	AuthUser                   // phai dang nhap
	AuthAdmin                  // phai la admin
)

// HandlerFunc xu ly mot yeu cau. Tra ve gia tri se duoc ma hoa JSON thanh payload phan hoi,
// hoac error de ra khung loi (xem StatusOf).
type HandlerFunc func(ctx context.Context, s *Session, payload json.RawMessage) (any, error)

type route struct {
	handler HandlerFunc
	auth    AuthLevel
}

// Router anh xa loai thong diep -> handler. Dang ky mot lan luc khoi dong, chi doc khi chay
// nen khong can khoa.
type Router struct {
	routes map[MsgType]route
}

func NewRouter() *Router {
	return &Router{routes: make(map[MsgType]route)}
}

// Handle dang ky handler cho loai thong diep t. Dang ky trung se panic de loi lo ngay luc khoi dong.
func (r *Router) Handle(t MsgType, auth AuthLevel, h HandlerFunc) {
	if _, dup := r.routes[t]; dup {
		panic("socket: handler dang ky trung cho " + t.String())
	}
	r.routes[t] = route{handler: h, auth: auth}
}

// Error la loi nghiep vu co ma ket qua ro rang de handler tra thang ve client.
type Error struct {
	Status  uint16
	Message string
}

func (e *Error) Error() string { return e.Message }

func NewError(status uint16, message string) *Error {
	return &Error{Status: status, Message: message}
}

// StatusOf doi mot error cua usecase/repository thanh (ma ket qua, thong bao cho client).
// Loi khong nhan dien duoc se thanh 500 va KHONG lo chi tiet noi bo ra ngoai.
func StatusOf(err error) (uint16, string) {
	var se *Error
	if errors.As(err, &se) {
		return se.Status, se.Message
	}
	var appErr *appErrors.AppError
	if errors.As(err, &appErr) {
		return StatusUnauthorized, appErr.Message
	}
	switch {
	case errors.Is(err, adapters.ErrFlightNotFound),
		errors.Is(err, adapters.ErrNoFlightsFound),
		errors.Is(err, adapters.ErrCustomerNotFound),
		errors.Is(err, adapters.ErrAdminNotFound),
		errors.Is(err, adapters.ErrTicketNotFound),
		errors.Is(err, adapters.ErrBookingNotFound),
		errors.Is(err, adapters.ErrSeatNotFound),
		errors.Is(err, pgx.ErrNoRows),
		errors.Is(err, sql.ErrNoRows):
		return StatusNotFound, err.Error()
	case errors.Is(err, adapters.ErrEmailInUse):
		return StatusConflict, err.Error()
	case errors.Is(err, context.DeadlineExceeded):
		return StatusTimeout, "request timed out"
	}
	return StatusInternal, "internal server error"
}
