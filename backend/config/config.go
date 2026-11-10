package config

import (
	"errors"
	"time"

	"github.com/spf13/viper"
)

// Config stores all configuration of the application.
// The values are read by viper from a config file or environment variable.
type Config struct {
	MailSenderName          string        `mapstructure:"MAIL_SENDER_NAME"`
	MailSenderAddress       string        `mapstructure:"MAIL_SENDER_ADDRESS"`
	MailSenderPassword      string        `mapstructure:"MAIL_SENDER_PASSWORD"`
	RedisAddress            string        `mapstructure:"REDIS_ADDRESS"`
	DBDriver                string        `mapstructure:"DB_DRIVER"`
	DBSource                string        `mapstructure:"DB_SOURCE"`
	ServerAddressPort       string        `mapstructure:"SERVER_ADDRESS_PORT"`
	TokenSymmetricKey       string        `mapstructure:"TOKEN_SYMMETRIC_KEY"`
	AccessTokenDuration     time.Duration `mapstructure:"ACCESS_TOKEN_DURATION"`
	AppEnv                  string        `mapstructure:"APP_ENV"`
	RateLimiterRequestSec   int           `mapstructure:"RATE_LIMITER_REQUEST_SEC"`
	RateLimiterRequestBurst int           `mapstructure:"RATE_LIMITER_REQUEST_BURST"`
	StripeSecretKey         string        `mapstructure:"STRIPE_SECRET_KEY"`
	RedisDB                 string        `mapstructure:"REDIS_DB"`
	RedisUsername           string        `mapstructure:"REDIS_USERNAME"`
	RedisPassword           string        `mapstructure:"REDIS_PASSWORD"`

	// Kenh noi bo giua netcore (C) va Go. Xem docs/BRIDGE-PROTOCOL.md.
	BridgeNetwork string `mapstructure:"BRIDGE_NETWORK"` // "tcp" hoac "unix"
	BridgeAddress string `mapstructure:"BRIDGE_ADDRESS"` // vd ":9100" hoac "/tmp/ltm-bridge.sock"
	BridgeLogPath string `mapstructure:"BRIDGE_LOG_PATH"`
}

// LoadConfig reads configuration from file or environment variables.
// File .env la tuy chon: neu khong co, moi gia tri lay tu bien moi truong.
func LoadConfig(path string) (config Config, err error) {
	viper.AddConfigPath(path)
	viper.SetConfigName(".env")
	viper.SetConfigType("env")

	// Phai khai bao tung khoa thi viper moi doc duoc gia tri tu bien moi truong khi khong co file .env.
	for _, key := range []string{
		"MAIL_SENDER_NAME", "MAIL_SENDER_ADDRESS", "MAIL_SENDER_PASSWORD", "REDIS_ADDRESS",
		"DB_DRIVER", "DB_SOURCE", "SERVER_ADDRESS_PORT", "TOKEN_SYMMETRIC_KEY", "ACCESS_TOKEN_DURATION",
		"APP_ENV", "RATE_LIMITER_REQUEST_SEC", "RATE_LIMITER_REQUEST_BURST", "STRIPE_SECRET_KEY",
		"REDIS_DB", "REDIS_USERNAME", "REDIS_PASSWORD",
		"BRIDGE_NETWORK", "BRIDGE_ADDRESS", "BRIDGE_LOG_PATH",
	} {
		_ = viper.BindEnv(key)
	}
	viper.SetDefault("BRIDGE_NETWORK", "tcp")
	viper.SetDefault("BRIDGE_ADDRESS", ":9100")
	viper.SetDefault("BRIDGE_LOG_PATH", "logs/bridge.log")
	viper.SetDefault("ACCESS_TOKEN_DURATION", "24h")
	viper.SetDefault("APP_ENV", "development")
	viper.AutomaticEnv()

	err = viper.ReadInConfig()
	var notFound viper.ConfigFileNotFoundError
	if err != nil && !errors.As(err, &notFound) {
		return
	}
	err = viper.Unmarshal(&config)
	return
}
