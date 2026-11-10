package db

import (
	"github.com/jackc/pgx/v5/pgxpool"
)

// Store gom moi cau truy van do sqlc sinh ra (Querier).
// Cac giao dich nhieu buoc (dat ve, huy ve, doi ghe...) se them vao day o giai doan 6 va 8.
type Store interface {
	Querier
}

// SQLStore hien thuc Store tren Postgres.
type SQLStore struct {
	connPool *pgxpool.Pool
	// Dung chung mot instance Queries, khong copy lai cho moi lan goi.
	*Queries
}

func NewStore(connPool *pgxpool.Pool) Store {
	return &SQLStore{
		connPool: connPool,
		Queries:  New(connPool),
	}
}
