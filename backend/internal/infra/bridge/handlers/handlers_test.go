package handlers_test

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"net"
	"strings"
	"testing"
	"time"

	"ltm-flight/internal/domain/adapters"
	"ltm-flight/internal/domain/entities"
	"ltm-flight/internal/domain/usecases/auth"
	"ltm-flight/internal/infra/api/dto"
	"ltm-flight/internal/infra/bridge"
	"ltm-flight/internal/infra/bridge/handlers"
	appErrors "ltm-flight/pkg/errors"
	"ltm-flight/pkg/token"
)

// ---------- usecase gia: chi ghi lai tham so va tra ket qua dat truoc ----------

type fakeHealth struct{}

func (fakeHealth) Execute() (entities.Health, error) {
	return entities.Health{Status: "OK", Version: "test"}, nil
}

type fakeLogin struct{ got auth.LoginInput }

func (f *fakeLogin) Execute(_ context.Context, in auth.LoginInput) (*auth.LoginOutput, error) {
	f.got = in
	if in.Password != "dung" {
		return nil, &appErrors.AppError{Message: "sai thong tin"}
	}
	return &auth.LoginOutput{Token: "tok", User: &entities.User{UserID: 42, Email: in.Email, FirstName: "A", LastName: "B", Role: entities.RoleCustomer}}, nil
}

type fakeCreateCustomer struct{ got entities.CreateUserParams }

func (f *fakeCreateCustomer) Execute(_ context.Context, in entities.CreateUserParams) (entities.User, error) {
	f.got = in
	if in.Email == "trung@example.com" {
		return entities.User{}, adapters.ErrEmailInUse
	}
	return entities.User{UserID: 7, FirstName: in.FirstName, LastName: in.LastName, Email: in.Email}, nil
}

type fakeGetFlight struct{ gotID int64 }

func (f *fakeGetFlight) Execute(_ context.Context, id int64) (*dto.GetFlightResponse, error) {
	f.gotID = id
	if id == 404 {
		return nil, adapters.ErrFlightNotFound
	}
	return &dto.GetFlightResponse{FlightID: "x", FlightNumber: "VN1"}, nil
}

type fakeSearch struct{ gotDep, gotArr, gotDate string }

func (f *fakeSearch) Execute(_ context.Context, dep, arr, date string) ([]dto.FlightSearchResponse, error) {
	f.gotDep, f.gotArr, f.gotDate = dep, arr, date
	if dep == "Khong co" {
		return nil, adapters.ErrNoFlightsFound
	}
	return []dto.FlightSearchResponse{{FlightNumber: "VN1"}}, nil
}

type fakeList struct{ gotPage, gotLimit int }

func (f *fakeList) Execute(_ context.Context, page, limit int) ([]dto.FlightSearchResponse, error) {
	f.gotPage, f.gotLimit = page, limit
	return nil, nil
}

type fakeTokenMaker struct{}

func (fakeTokenMaker) CreateToken(int64, string, time.Duration, token.TokenType) (string, *token.Payload, error) {
	return "", nil, errors.New("khong dung")
}

func (fakeTokenMaker) VerifyToken(tok string, _ token.TokenType) (*token.Payload, error) {
	if tok != "tot" {
		return nil, token.ErrInvalidToken
	}
	return &token.Payload{UserId: 5, Role: "admin"}, nil
}

// ---------- ha tang test: server that + "netcore" gia ----------

type fixture struct {
	t      *testing.T
	conn   net.Conn
	br     *bufio.Reader
	login  *fakeLogin
	create *fakeCreateCustomer
	flight *fakeGetFlight
	search *fakeSearch
	list   *fakeList
	reqID  uint64
}

func newFixture(t *testing.T) *fixture {
	t.Helper()
	fx := &fixture{t: t, login: &fakeLogin{}, create: &fakeCreateCustomer{}, flight: &fakeGetFlight{}, search: &fakeSearch{}, list: &fakeList{}}

	router := bridge.NewRouter()
	handlers.Register(router, handlers.Deps{
		Health:         fakeHealth{},
		Login:          fx.login,
		CreateCustomer: fx.create,
		GetFlight:      fx.flight,
		SearchFlights:  fx.search,
		ListFlights:    fx.list,
		TokenMaker:     fakeTokenMaker{},
	})
	srv := bridge.NewServer(bridge.Config{RequestTimeout: time.Second}, router)
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() { defer close(done); _ = srv.Serve(ctx, ln) }()
	t.Cleanup(func() { cancel(); <-done })

	fx.conn, err = net.Dial("tcp", ln.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { fx.conn.Close() })
	fx.br = bufio.NewReader(fx.conn)
	return fx
}

// call gui mot yeu cau tren conn_id 1 va tra ve (status, payload).
func (fx *fixture) call(typ bridge.MsgType, payload string) (uint16, string) {
	fx.t.Helper()
	fx.reqID++
	f := &bridge.Frame{Header: bridge.Header{Kind: bridge.KindRequest, Type: typ, ConnID: 1, ReqID: fx.reqID}, Payload: []byte(payload)}
	if err := bridge.WriteFrame(fx.conn, f); err != nil {
		fx.t.Fatal(err)
	}
	_ = fx.conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	resp, err := bridge.ReadFrame(fx.br)
	if err != nil {
		fx.t.Fatalf("khong nhan duoc phan hoi: %v", err)
	}
	return resp.Status, string(resp.Payload)
}

func wantStatus(t *testing.T, what string, got, want uint16, payload string) {
	t.Helper()
	if got != want {
		t.Errorf("%s: status=%d, muon %d (payload %s)", what, got, want, payload)
	}
}

// ---------- test ----------

func TestRegisterValidation(t *testing.T) {
	fx := newFixture(t)
	cases := []struct {
		name    string
		payload string
		want    uint16
	}{
		{"thieu truong", `{"first_name":"A","email":"a@b.c","password":"123456"}`, bridge.StatusBadRequest},
		{"email sai", `{"first_name":"A","last_name":"B","email":"khong-co-a-cong","password":"123456"}`, bridge.StatusBadRequest},
		{"mat khau ngan", `{"first_name":"A","last_name":"B","email":"a@b.c","password":"123"}`, bridge.StatusBadRequest},
		{"mat khau qua 72 byte", `{"first_name":"A","last_name":"B","email":"a@b.c","password":"` + strings.Repeat("x", 73) + `"}`, bridge.StatusBadRequest},
		{"json hong", `{{{`, bridge.StatusBadRequest},
		{"hop le", `{"first_name":"A","last_name":"B","email":" a@b.c ","password":"123456"}`, bridge.StatusOK},
		{"email da dung", `{"first_name":"A","last_name":"B","email":"trung@example.com","password":"123456"}`, bridge.StatusConflict},
	}
	for _, c := range cases {
		status, payload := fx.call(bridge.MsgAuthRegister, c.payload)
		wantStatus(t, c.name, status, c.want, payload)
	}
	status, _ := fx.call(bridge.MsgAuthRegister, `{"first_name":"A","last_name":"B","email":"  trim@b.c  ","password":"123456"}`)
	wantStatus(t, "trim email", status, bridge.StatusOK, "")
	if fx.create.got.Email != "trim@b.c" {
		t.Errorf("email phai duoc cat khoang trang, usecase nhan %q", fx.create.got.Email)
	}
}

func TestLoginSetsIdentityAndLogoutClearsIt(t *testing.T) {
	fx := newFixture(t)

	status, payload := fx.call(bridge.MsgAuthLogin, `{"email":"a@b.c","password":"sai"}`)
	wantStatus(t, "sai mat khau", status, bridge.StatusUnauthorized, payload)
	if status, _ := fx.call(bridge.MsgAuthLogout, `{}`); status != bridge.StatusUnauthorized {
		t.Errorf("dang xuat khi chua dang nhap phai 401, nhan %d", status)
	}

	status, payload = fx.call(bridge.MsgAuthLogin, `{"email":"a@b.c","password":"dung"}`)
	wantStatus(t, "dang nhap dung", status, bridge.StatusOK, payload)
	var resp struct {
		Token string `json:"token"`
		User  struct {
			ID   int64  `json:"id"`
			Role string `json:"role"`
		} `json:"user"`
	}
	if err := json.Unmarshal([]byte(payload), &resp); err != nil || resp.Token != "tok" || resp.User.ID != 42 {
		t.Errorf("payload dang nhap sai: %s", payload)
	}

	status, payload = fx.call(bridge.MsgAuthLogout, `{}`)
	wantStatus(t, "dang xuat", status, bridge.StatusOK, payload)
	if status, _ := fx.call(bridge.MsgAuthLogout, `{}`); status != bridge.StatusUnauthorized {
		t.Errorf("sau khi dang xuat phai tro lai la khach (401), nhan %d", status)
	}

	status, payload = fx.call(bridge.MsgAuthLogin, `{"email":"","password":""}`)
	wantStatus(t, "thieu thong tin", status, bridge.StatusBadRequest, payload)
}

func TestAuthToken(t *testing.T) {
	fx := newFixture(t)
	status, payload := fx.call(bridge.MsgAuthToken, `{"token":"hong"}`)
	wantStatus(t, "token hong", status, bridge.StatusUnauthorized, payload)
	status, payload = fx.call(bridge.MsgAuthToken, `{"token":"tot"}`)
	wantStatus(t, "token tot", status, bridge.StatusOK, payload)
	// Sau khi gan danh tinh bang token, AUTH_LOGOUT (yeu cau dang nhap) phai chay duoc.
	status, payload = fx.call(bridge.MsgAuthLogout, `{}`)
	wantStatus(t, "logout sau token", status, bridge.StatusOK, payload)
}

func TestFlightSearch(t *testing.T) {
	fx := newFixture(t)

	status, payload := fx.call(bridge.MsgFlightSearch, `{"departureCity":"Ha Noi","arrivalCity":"Da Nang"}`)
	wantStatus(t, "thieu ngay", status, bridge.StatusBadRequest, payload)
	status, payload = fx.call(bridge.MsgFlightSearch, `{"departureCity":"Ha Noi","arrivalCity":"Da Nang","flightDate":"31/12/2026"}`)
	wantStatus(t, "ngay sai dinh dang", status, bridge.StatusBadRequest, payload)

	status, payload = fx.call(bridge.MsgFlightSearch, `{"departureCity":"Ha Noi","arrivalCity":"Da Nang","flightDate":"2026-12-31"}`)
	wantStatus(t, "ngay YYYY-MM-DD", status, bridge.StatusOK, payload)
	if fx.search.gotDate != "2026-12-31T00:00:00Z" {
		t.Errorf("usecase phai nhan ngay chuan hoa, nhan %q", fx.search.gotDate)
	}
	status, payload = fx.call(bridge.MsgFlightSearch, `{"departureCity":"Ha Noi","arrivalCity":"Da Nang","flightDate":"2026-12-31T00:00:00Z"}`)
	wantStatus(t, "ngay RFC3339", status, bridge.StatusOK, payload)

	status, payload = fx.call(bridge.MsgFlightSearch, `{"departureCity":"Khong co","arrivalCity":"Da Nang","flightDate":"2026-12-31"}`)
	wantStatus(t, "khong co chuyen", status, bridge.StatusNotFound, payload)
}

func TestFlightGetAcceptsNumberOrString(t *testing.T) {
	fx := newFixture(t)
	for _, p := range []string{`{"flightId":12}`, `{"flightId":"12"}`} {
		status, payload := fx.call(bridge.MsgFlightGet, p)
		wantStatus(t, p, status, bridge.StatusOK, payload)
		if fx.flight.gotID != 12 {
			t.Errorf("%s: usecase nhan id=%d, muon 12", p, fx.flight.gotID)
		}
	}
	for _, p := range []string{`{}`, `{"flightId":0}`, `{"flightId":"abc"}`} {
		status, payload := fx.call(bridge.MsgFlightGet, p)
		wantStatus(t, p, status, bridge.StatusBadRequest, payload)
	}
	status, payload := fx.call(bridge.MsgFlightGet, `{"flightId":404}`)
	wantStatus(t, "khong ton tai", status, bridge.StatusNotFound, payload)
}

func TestFlightListPagingDefaultsAndClamp(t *testing.T) {
	fx := newFixture(t)
	cases := []struct {
		payload     string
		page, limit int
	}{
		{`{}`, 1, 10},
		{`{"page":3,"limit":25}`, 3, 25},
		{`{"page":-5,"limit":-1}`, 1, 10},
		{`{"page":2,"limit":100000}`, 2, 100},
	}
	for _, c := range cases {
		status, payload := fx.call(bridge.MsgFlightList, c.payload)
		wantStatus(t, c.payload, status, bridge.StatusOK, payload)
		if fx.list.gotPage != c.page || fx.list.gotLimit != c.limit {
			t.Errorf("%s: usecase nhan page=%d limit=%d, muon %d/%d", c.payload, fx.list.gotPage, fx.list.gotLimit, c.page, c.limit)
		}
	}
}

func TestSysHealth(t *testing.T) {
	fx := newFixture(t)
	status, payload := fx.call(bridge.MsgSysHealth, ``)
	wantStatus(t, "health", status, bridge.StatusOK, payload)
}
