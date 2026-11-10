// Command server chay phan Go cua he thong: nhan yeu cau tu netcore (C) qua kenh noi bo
// (infra/bridge), goi usecase va tra phan hoi. Khong con HTTP/REST.
package main

import (
	"context"
	"io"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
	"ltm-flight/config"
	db "ltm-flight/db/sqlc"
	"ltm-flight/internal/domain/usecases"
	"ltm-flight/internal/domain/usecases/auth"
	"ltm-flight/internal/domain/usecases/customer"
	"ltm-flight/internal/domain/usecases/flight"
	"ltm-flight/internal/infra/bridge"
	"ltm-flight/internal/infra/bridge/handlers"
	"ltm-flight/internal/infra/postgresql"
	"ltm-flight/pkg/token"
	"ltm-flight/pkg/utils"
)

func main() {
	cfg, err := config.LoadConfig(".")
	if err != nil {
		log.Fatal("cannot load config: ", err)
	}
	if err := utils.LoadMessages("./message.json"); err != nil {
		log.Fatal("cannot load message.json: ", err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	// --- Ket noi database ---
	poolConfig, err := pgxpool.ParseConfig(cfg.DBSource)
	if err != nil {
		log.Fatal("cannot parse DB_SOURCE: ", err)
	}
	poolConfig.MaxConns = 20
	poolConfig.MinConns = 2
	poolConfig.MaxConnLifetime = 30 * time.Minute
	poolConfig.HealthCheckPeriod = time.Minute

	pool, err := pgxpool.NewWithConfig(ctx, poolConfig)
	if err != nil {
		log.Fatal("cannot connect to database: ", err)
	}
	defer pool.Close()
	if err := pool.Ping(ctx); err != nil {
		log.Fatal("database is not reachable: ", err)
	}
	store := db.NewStore(pool)

	// --- Repository -> usecase (tang domain giu nguyen tu Qairlines) ---
	tokenMaker, err := token.NewPasetoMaker(cfg.TokenSymmetricKey)
	if err != nil {
		log.Fatal("cannot create token maker: ", err)
	}
	userRepo := postgresql.NewUserRepositoryPostgres(&store, tokenMaker)
	customerRepo := postgresql.NewCustomerRepositoryPostgres(&store, tokenMaker)
	flightRepo := postgresql.NewFlightRepositoryPostgres(&store)
	healthRepo := postgresql.NewHealthRepositoryPostgres(&store)

	deps := handlers.Deps{
		Health:         usecases.NewHealthUseCase(healthRepo),
		Login:          auth.NewLoginUseCase(userRepo, tokenMaker),
		CreateCustomer: customer.NewCreateCustomerUseCase(customerRepo, userRepo),
		GetFlight:      flight.NewGetFlightUseCase(flightRepo),
		SearchFlights:  flight.NewSearchFlightsUseCase(flightRepo),
		ListFlights:    flight.NewlistFlightsUseCase(flightRepo),
		TokenMaker:     tokenMaker,
	}

	// --- Server socket ---
	router := bridge.NewRouter()
	handlers.Register(router, deps)

	logWriter, closeLog := openFrameLog(cfg.BridgeLogPath)
	defer closeLog()

	srv := bridge.NewServer(bridge.Config{
		Network: cfg.BridgeNetwork,
		Address: cfg.BridgeAddress,
		Logger:  bridge.NewTextLogger(logWriter, cfg.AppEnv == "development"),
	}, router)

	log.Printf("bridge dang lang nghe tren %s %s (cho netcore ket noi)", cfg.BridgeNetwork, cfg.BridgeAddress)
	if err := srv.ListenAndServe(ctx); err != nil {
		log.Fatal("bridge server stopped: ", err)
	}
	log.Println("da dung server")
}

// openFrameLog ghi log khung ra stdout va file (neu mo duoc file).
func openFrameLog(path string) (io.Writer, func()) {
	if path == "" {
		return os.Stdout, func() {}
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		log.Printf("khong tao duoc thu muc log: %v", err)
		return os.Stdout, func() {}
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		log.Printf("khong mo duoc file log %s: %v", path, err)
		return os.Stdout, func() {}
	}
	return io.MultiWriter(os.Stdout, f), func() { f.Close() }
}
