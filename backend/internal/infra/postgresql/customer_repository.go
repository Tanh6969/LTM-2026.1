package postgresql

import (
	"context"
	"errors"
	"fmt"
	"strconv"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgtype"
	db "ltm-flight/db/sqlc"
	"ltm-flight/internal/domain/adapters"
	"ltm-flight/internal/domain/entities"
	"ltm-flight/pkg/token"
	"ltm-flight/pkg/utils"
)

// CustomerRepositoryPostgres: bang Customers da duoc gop vao Users (role = 'customer').
type CustomerRepositoryPostgres struct {
	store      db.Store
	tokenMaker token.Maker
}

func NewCustomerRepositoryPostgres(store *db.Store, tokenMaker token.Maker) *CustomerRepositoryPostgres {
	return &CustomerRepositoryPostgres{
		store:      *store,
		tokenMaker: tokenMaker,
	}
}

func (r *CustomerRepositoryPostgres) CreateCustomerTx(ctx context.Context, arg entities.CreateUserParams) (entities.User, error) {
	hashedPassword, err := utils.HashPassword(arg.Password)
	if err != nil {
		return entities.User{}, err
	}
	user, err := r.store.CreateUser(ctx, db.CreateUserParams{
		Email:                arg.Email,
		HashedPassword:       hashedPassword,
		FirstName:            pgtype.Text{String: arg.FirstName, Valid: true},
		LastName:             pgtype.Text{String: arg.LastName, Valid: true},
		Role:                 db.UserRoleCustomer,
		PhoneNumber:          pgtype.Text{String: "", Valid: true},
		Gender:               db.GenderTypeOther,
		DateOfBirth:          pgtype.Date{}, // chua biet ngay sinh -> NULL
		PassportNumber:       pgtype.Text{String: "", Valid: true},
		IdentificationNumber: pgtype.Text{String: "", Valid: true},
		Address:              pgtype.Text{String: "", Valid: true},
	})
	if err != nil {
		return entities.User{}, err
	}
	return toUserEntity(user), nil
}

func (r *CustomerRepositoryPostgres) UpdateCustomer(ctx context.Context, customer entities.Customer, user entities.User) (entities.Customer, entities.User, error) {
	err := r.store.UpdateUser(ctx, db.UpdateUserParams{
		UserID:               customer.UserID,
		FirstName:            pgtype.Text{String: user.FirstName, Valid: true},
		LastName:             pgtype.Text{String: user.LastName, Valid: true},
		PhoneNumber:          pgtype.Text{String: customer.PhoneNumber, Valid: true},
		Gender:               db.GenderType(customer.Gender),
		DateOfBirth:          toPgDate(customer.DateOfBirth),
		PassportNumber:       pgtype.Text{String: customer.PassportNumber, Valid: true},
		IdentificationNumber: pgtype.Text{String: customer.IdentificationNumber, Valid: true},
		Address:              pgtype.Text{String: customer.Address, Valid: true},
	})
	if err != nil {
		return entities.Customer{}, entities.User{}, err
	}

	updated, err := r.GetCustomerByUID(ctx, customer.UserID)
	if err != nil {
		return entities.Customer{}, entities.User{}, err
	}
	return *updated, updated.User, nil
}

func (r *CustomerRepositoryPostgres) ListCustomers(ctx context.Context, offset int, limit int) ([]entities.Customer, error) {
	rows, err := r.store.ListCustomers(ctx, db.ListCustomersParams{
		Limit:  int32(limit),
		Offset: int32(offset),
	})
	if err != nil {
		return nil, fmt.Errorf("failed to list customers: %w", err)
	}

	customers := make([]entities.Customer, 0, len(rows))
	for _, row := range rows {
		customers = append(customers, toCustomerEntity(row))
	}
	return customers, nil
}

// DeleteCustomerByID xoa mem: giu lai don dat ve cu, chi danh dau tai khoan khong con hoat dong.
func (r *CustomerRepositoryPostgres) DeleteCustomerByID(ctx context.Context, customerID int64) error {
	deletedID, err := r.store.SoftDeleteCustomer(ctx, customerID)
	if errors.Is(err, pgx.ErrNoRows) || (err == nil && deletedID == 0) {
		return adapters.ErrCustomerNotFound
	}
	if err != nil {
		return fmt.Errorf("failed to delete customer: %w", err)
	}
	return nil
}

func (r *CustomerRepositoryPostgres) GetCustomerByUID(ctx context.Context, uid int64) (*entities.Customer, error) {
	row, err := r.store.GetCustomerByID(ctx, uid)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, adapters.ErrCustomerNotFound
	}
	if err != nil {
		return nil, fmt.Errorf("failed to get customer by UID: %w", err)
	}

	return &entities.Customer{
		UserID: row.Uid,
		User: entities.User{
			UserID:    row.Uid,
			FirstName: row.FirstName.String,
			LastName:  row.LastName.String,
			Email:     row.Email,
		},
		PhoneNumber:          row.PhoneNumber.String,
		DateOfBirth:          row.DateOfBirth.Time,
		Gender:               entities.CustomerGender(row.Gender),
		IdentificationNumber: row.IdentificationNumber.String,
		PassportNumber:       row.PassportNumber.String,
		Address:              row.Address.String,
		LoyaltyPoints:        row.LoyaltyPoints.Int32,
	}, nil
}

func (r *CustomerRepositoryPostgres) GetBookingHistoryByUID(ctx context.Context, uid int64) ([]string, error) {
	rows, err := r.store.GetBookingHistoryByUID(ctx, pgtype.Int8{Int64: uid, Valid: true})
	if err != nil {
		return nil, fmt.Errorf("failed to get booking history by UID: %w", err)
	}

	history := make([]string, 0, len(rows))
	for _, bookingID := range rows {
		history = append(history, strconv.FormatInt(bookingID, 10))
	}
	return history, nil
}

// toUserEntity doi mot dong users cua sqlc sang entity User cua domain.
func toUserEntity(u db.User) entities.User {
	return entities.User{
		UserID:    u.UserID,
		Email:     u.Email,
		HashedPwd: u.HashedPassword,
		FirstName: u.FirstName.String,
		LastName:  u.LastName.String,
		Role:      entities.UserRole(u.Role),
		IsActive:  u.IsActive,
		CreatedAt: u.CreatedAt,
		UpdatedAt: u.UpdatedAt,
	}
}

// toCustomerEntity doi mot dong users (role customer) sang entity Customer.
func toCustomerEntity(u db.User) entities.Customer {
	return entities.Customer{
		UserID:               u.UserID,
		User:                 toUserEntity(u),
		PhoneNumber:          u.PhoneNumber.String,
		DateOfBirth:          u.DateOfBirth.Time,
		Gender:               entities.CustomerGender(u.Gender),
		PassportNumber:       u.PassportNumber.String,
		IdentificationNumber: u.IdentificationNumber.String,
		Address:              u.Address.String,
		LoyaltyPoints:        u.LoyaltyPoints.Int32,
	}
}

// toPgDate doi time.Time sang cot DATE cho phep NULL; thoi diem 0 nghia la chua co ngay sinh.
func toPgDate(t time.Time) pgtype.Date {
	if t.IsZero() {
		return pgtype.Date{}
	}
	return pgtype.Date{Time: t, Valid: true}
}
