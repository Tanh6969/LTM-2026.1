package handlers

import (
	"context"
	"encoding/json"
	"strings"

	"ltm-flight/internal/domain/entities"
	"ltm-flight/internal/domain/usecases/auth"
	"ltm-flight/internal/infra/api/dto"
	"ltm-flight/internal/infra/api/mappers"
	"ltm-flight/internal/infra/bridge"
	"ltm-flight/pkg/token"
)

// bcrypt chi bam toi da 72 byte dau cua mat khau.
const (
	minPasswordLen = 6
	maxPasswordLen = 72
)

func registerAuth(r *bridge.Router, d Deps) {
	// AUTH_REGISTER: dang ky tai khoan khach hang. Khach chua dang nhap van goi duoc.
	r.Handle(bridge.MsgAuthRegister, bridge.AuthNone, func(ctx context.Context, s *bridge.Session, raw json.RawMessage) (any, error) {
		var req dto.CreateCustomerRequest
		if err := decode(raw, &req); err != nil {
			return nil, err
		}
		req.Email = strings.TrimSpace(req.Email)
		if req.FirstName == "" || req.LastName == "" || req.Email == "" || req.Password == "" {
			return nil, badRequest("all fields are required")
		}
		if !strings.Contains(req.Email, "@") {
			return nil, badRequest("invalid email")
		}
		if len(req.Password) < minPasswordLen || len(req.Password) > maxPasswordLen {
			return nil, badRequest("password must be 6 to 72 characters")
		}

		user, err := d.CreateCustomer.Execute(ctx, entities.CreateUserParams{
			FirstName: req.FirstName,
			LastName:  req.LastName,
			Email:     req.Email,
			Password:  req.Password,
		})
		if err != nil {
			return nil, err
		}
		return mappers.CreateCustomerGetOutputToResponse(user), nil
	})

	// AUTH_LOGIN: dang nhap; thanh cong thi ket noi nay duoc gan danh tinh va nhan token.
	r.Handle(bridge.MsgAuthLogin, bridge.AuthNone, func(ctx context.Context, s *bridge.Session, raw json.RawMessage) (any, error) {
		var req auth.LoginInput
		if err := decode(raw, &req); err != nil {
			return nil, err
		}
		if req.Email == "" || req.Password == "" {
			return nil, badRequest("email and password are required")
		}

		out, err := d.Login.Execute(ctx, req)
		if err != nil {
			return nil, err
		}
		s.SetIdentity(out.User.UserID, string(out.User.Role))
		return mappers.LoginOutputToResponse(*out), nil
	})

	// AUTH_LOGOUT: bo danh tinh cua ket noi nay (token da cap van hieu luc den khi het han).
	r.Handle(bridge.MsgAuthLogout, bridge.AuthUser, func(ctx context.Context, s *bridge.Session, _ json.RawMessage) (any, error) {
		s.ClearIdentity()
		return map[string]string{"message": "logged out"}, nil
	})

	// AUTH_TOKEN: ket noi moi (vd sau khi tai lai trang) gan lai danh tinh bang token da co.
	r.Handle(bridge.MsgAuthToken, bridge.AuthNone, func(ctx context.Context, s *bridge.Session, raw json.RawMessage) (any, error) {
		var req struct {
			Token string `json:"token"`
		}
		if err := decode(raw, &req); err != nil {
			return nil, err
		}
		if req.Token == "" {
			return nil, badRequest("token is required")
		}
		payload, err := d.TokenMaker.VerifyToken(req.Token, token.TokenTypeAccessToken)
		if err != nil {
			return nil, bridge.NewError(bridge.StatusUnauthorized, "invalid or expired token")
		}
		s.SetIdentity(payload.UserId, payload.Role)
		return map[string]any{"userId": payload.UserId, "role": payload.Role}, nil
	})
}
