package postgresql

import (
	"context"
	"errors"
	"fmt"
	"strconv"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgtype"
	db "ltm-flight/db/sqlc"
	"ltm-flight/internal/domain/adapters"
	"ltm-flight/internal/domain/entities"
	"ltm-flight/pkg/token"
)

// AdminRepositoryPostgres: bang Admins da duoc gop vao Users (role = 'admin').
type AdminRepositoryPostgres struct {
	store      db.Store
	tokenMaker token.Maker
}

func NewAdminRepositoryPostgres(store *db.Store, tokenMaker token.Maker) *AdminRepositoryPostgres {
	return &AdminRepositoryPostgres{
		store:      *store,
		tokenMaker: tokenMaker,
	}
}

// CreateAdminTx: usecase da bam mat khau truoc khi goi (arg.Password la hash).
func (r *AdminRepositoryPostgres) CreateAdminTx(ctx context.Context, arg entities.CreateUserParams) (entities.User, error) {
	user, err := r.store.CreateUser(ctx, db.CreateUserParams{
		Email:                arg.Email,
		HashedPassword:       arg.Password,
		FirstName:            pgtype.Text{String: arg.FirstName, Valid: true},
		LastName:             pgtype.Text{String: arg.LastName, Valid: true},
		Role:                 db.UserRoleAdmin,
		PhoneNumber:          pgtype.Text{String: "", Valid: true},
		Gender:               db.GenderTypeOther,
		PassportNumber:       pgtype.Text{String: "", Valid: true},
		IdentificationNumber: pgtype.Text{String: "", Valid: true},
		Address:              pgtype.Text{String: "", Valid: true},
	})
	if err != nil {
		return entities.User{}, err
	}
	return toUserEntity(user), nil
}

func (r *AdminRepositoryPostgres) ListAdmins(ctx context.Context, offset int, limit int) ([]entities.Admin, error) {
	rows, err := r.store.ListAdmins(ctx, db.ListAdminsParams{
		Limit:  int32(limit),
		Offset: int32(offset),
	})
	if err != nil {
		return nil, err
	}

	admins := make([]entities.Admin, len(rows))
	for i, row := range rows {
		admins[i] = toAdminEntity(row)
	}
	return admins, nil
}

func (r *AdminRepositoryPostgres) GetAdminByID(ctx context.Context, adminID string) (entities.Admin, error) {
	id, err := strconv.ParseInt(adminID, 10, 64)
	if err != nil {
		return entities.Admin{}, err
	}
	return r.GetAdminByUserID(ctx, id)
}

func (r *AdminRepositoryPostgres) GetAdminByUserID(ctx context.Context, userID int64) (entities.Admin, error) {
	user, err := r.store.GetAdmin(ctx, userID)
	if errors.Is(err, pgx.ErrNoRows) {
		return entities.Admin{}, adapters.ErrAdminNotFound
	}
	if err != nil {
		return entities.Admin{}, err
	}
	return toAdminEntity(user), nil
}

func (r *AdminRepositoryPostgres) UpdateAdmin(ctx context.Context, admin entities.Admin) (entities.Admin, error) {
	current, err := r.store.GetAdmin(ctx, admin.UserID)
	if errors.Is(err, pgx.ErrNoRows) {
		return entities.Admin{}, adapters.ErrAdminNotFound
	}
	if err != nil {
		return entities.Admin{}, err
	}

	// UpdateUser ghi de toan bo cot ho so, nen giu nguyen gia tri hien co cua cac cot khong doi.
	err = r.store.UpdateUser(ctx, db.UpdateUserParams{
		UserID:               admin.UserID,
		FirstName:            pgtype.Text{String: admin.FirstName, Valid: true},
		LastName:             pgtype.Text{String: admin.LastName, Valid: true},
		PhoneNumber:          current.PhoneNumber,
		Gender:               current.Gender,
		DateOfBirth:          current.DateOfBirth,
		PassportNumber:       current.PassportNumber,
		IdentificationNumber: current.IdentificationNumber,
		Address:              current.Address,
	})
	if err != nil {
		return entities.Admin{}, err
	}

	updated, err := r.store.GetAdmin(ctx, admin.UserID)
	if err != nil {
		return entities.Admin{}, err
	}
	return toAdminEntity(updated), nil
}

// DeleteAdmin: cac bang tham chieu (News, FlightStatusHistory) da dat ON DELETE SET NULL
// nen chi can kiem tra dung la admin roi xoa dong users.
func (r *AdminRepositoryPostgres) DeleteAdmin(ctx context.Context, userID int64) error {
	if _, err := r.store.GetAdmin(ctx, userID); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return adapters.ErrAdminNotFound
		}
		return fmt.Errorf("failed to delete admin: %w", err)
	}
	if err := r.store.DeleteUser(ctx, userID); err != nil {
		return fmt.Errorf("failed to delete admin: %w", err)
	}
	return nil
}

func toAdminEntity(u db.User) entities.Admin {
	return entities.Admin{
		UserID:    u.UserID,
		FirstName: u.FirstName.String,
		LastName:  u.LastName.String,
		Email:     u.Email,
		CreatedAt: u.CreatedAt,
	}
}
