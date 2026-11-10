package config

import (
	"errors"
	"time"

	"github.com/spf13/viper"
)

// Config stores all configuration of the application.
// The values are read by viper from a config file or environment variable.
type Config struct {
	DBSource            string        `mapstructure:"DB_SOURCE"`
	TokenSymmetricKey   string        `mapstructure:"TOKEN_SYMMETRIC_KEY"`
	AccessTokenDuration time.Duration `mapstructure:"ACCESS_TOKEN_DURATION"`
	AppEnv              string        `mapstructure:"APP_ENV"`

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
		"DB_SOURCE", "TOKEN_SYMMETRIC_KEY", "ACCESS_TOKEN_DURATION", "APP_ENV",
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
