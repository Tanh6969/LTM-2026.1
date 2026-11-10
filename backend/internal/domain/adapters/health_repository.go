package adapters

import (
	"context"

	"ltm-flight/internal/domain/entities"
)

type IHealthRepository interface {
	GetHealth(ctx context.Context) (entities.Health, error)
}
