package handlers

import (
	"context"
	"encoding/json"

	"ltm-flight/internal/infra/bridge"
)

func registerSystem(r *bridge.Router, d Deps) {
	// SYS_HEALTH: kiem tra nhanh server Go con song va do tai CPU.
	r.Handle(bridge.MsgSysHealth, bridge.AuthNone, func(ctx context.Context, s *bridge.Session, _ json.RawMessage) (any, error) {
		h, err := d.Health.Execute()
		if err != nil {
			return nil, err
		}
		return map[string]any{
			"status":     h.Status,
			"version":    h.Version,
			"cpuPercent": h.Stats.CPUPercent,
			"cpuCore":    h.Stats.CPUCore,
		}, nil
	})
}
