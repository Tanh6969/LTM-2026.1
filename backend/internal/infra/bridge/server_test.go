package bridge

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"net"
	"sync"
	"testing"
	"time"

	"ltm-flight/internal/domain/adapters"
)

// fakeNetcore dong vai netcore (C): ket noi vao server Go va gui/nhan khung.
type fakeNetcore struct {
	t    *testing.T
	conn net.Conn
	br   *bufio.Reader
}

func startServer(t *testing.T, router *Router) (*Server, *fakeNetcore) {
	t.Helper()
	srv := NewServer(Config{RequestTimeout: time.Second}, router)
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() { defer close(done); _ = srv.Serve(ctx, ln) }()
	t.Cleanup(func() { cancel(); <-done })

	conn, err := net.Dial("tcp", ln.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close() })
	return srv, &fakeNetcore{t: t, conn: conn, br: bufio.NewReader(conn)}
}

func (n *fakeNetcore) send(f *Frame) {
	n.t.Helper()
	if err := WriteFrame(n.conn, f); err != nil {
		n.t.Fatal(err)
	}
}

func (n *fakeNetcore) recv() *Frame {
	n.t.Helper()
	_ = n.conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	f, err := ReadFrame(n.br)
	if err != nil {
		n.t.Fatalf("khong nhan duoc khung: %v", err)
	}
	return f
}

func (n *fakeNetcore) request(connID uint32, reqID uint64, typ MsgType, payload string) *Frame {
	n.send(&Frame{Header: Header{Kind: KindRequest, Type: typ, ConnID: connID, ReqID: reqID}, Payload: []byte(payload)})
	return n.recv()
}

const (
	msgEcho   MsgType = 0x7001
	msgSecret MsgType = 0x7002
	msgAdmin  MsgType = 0x7003
	msgPanic  MsgType = 0x7004
	msgLogin  MsgType = 0x7005
	msgFail   MsgType = 0x7006
	msgSlow   MsgType = 0x7007
)

func testRouter() *Router {
	r := NewRouter()
	r.Handle(msgEcho, AuthNone, func(ctx context.Context, s *Session, p json.RawMessage) (any, error) {
		return json.RawMessage(p), nil
	})
	r.Handle(msgSecret, AuthUser, func(ctx context.Context, s *Session, p json.RawMessage) (any, error) {
		id, _ := s.Identity()
		return map[string]int64{"user": id}, nil
	})
	r.Handle(msgAdmin, AuthAdmin, func(ctx context.Context, s *Session, p json.RawMessage) (any, error) {
		return nil, nil
	})
	r.Handle(msgPanic, AuthNone, func(ctx context.Context, s *Session, p json.RawMessage) (any, error) {
		panic("boom")
	})
	r.Handle(msgLogin, AuthNone, func(ctx context.Context, s *Session, p json.RawMessage) (any, error) {
		var req struct {
			ID   int64  `json:"id"`
			Role string `json:"role"`
		}
		if err := json.Unmarshal(p, &req); err != nil {
			return nil, NewError(StatusBadRequest, "bad")
		}
		s.SetIdentity(req.ID, req.Role)
		return nil, nil
	})
	r.Handle(msgFail, AuthNone, func(ctx context.Context, s *Session, p json.RawMessage) (any, error) {
		var which string
		_ = json.Unmarshal(p, &which)
		switch which {
		case "notfound":
			return nil, adapters.ErrFlightNotFound
		case "conflict":
			return nil, adapters.ErrEmailInUse
		}
		return nil, errors.New("loi noi bo bi mat: password=hunter2")
	})
	r.Handle(msgSlow, AuthNone, func(ctx context.Context, s *Session, p json.RawMessage) (any, error) {
		<-ctx.Done()
		return nil, ctx.Err()
	})
	return r
}

func TestRequestResponse(t *testing.T) {
	_, nc := startServer(t, testRouter())
	resp := nc.request(5, 42, msgEcho, `{"hello":"world"}`)
	if resp.Kind != KindResponse || resp.Type != msgEcho || resp.ConnID != 5 || resp.ReqID != 42 || resp.Status != StatusOK {
		t.Fatalf("phan hoi sai header: %+v", resp.Header)
	}
	if string(resp.Payload) != `{"hello":"world"}` {
		t.Fatalf("payload = %s", resp.Payload)
	}
}

func TestUnknownTypeAndAuth(t *testing.T) {
	_, nc := startServer(t, testRouter())

	if r := nc.request(1, 1, 0x7FFF, `{}`); r.Status != StatusUnknownType {
		t.Errorf("loai la: status=%d", r.Status)
	}
	if r := nc.request(1, 2, msgSecret, `{}`); r.Status != StatusUnauthorized {
		t.Errorf("chua dang nhap: status=%d", r.Status)
	}

	nc.request(1, 3, msgLogin, `{"id":9,"role":"customer"}`)
	if r := nc.request(1, 4, msgSecret, `{}`); r.Status != StatusOK || string(r.Payload) != `{"user":9}` {
		t.Errorf("da dang nhap: status=%d payload=%s", r.Status, r.Payload)
	}
	if r := nc.request(1, 5, msgAdmin, `{}`); r.Status != StatusForbidden {
		t.Errorf("khach khong duoc vao admin: status=%d", r.Status)
	}
	// Danh tinh gan theo conn_id: client khac van la khach.
	if r := nc.request(2, 6, msgSecret, `{}`); r.Status != StatusUnauthorized {
		t.Errorf("conn khac phai chua dang nhap: status=%d", r.Status)
	}

	nc.request(3, 7, msgLogin, `{"id":1,"role":"admin"}`)
	if r := nc.request(3, 8, msgAdmin, `{}`); r.Status != StatusOK {
		t.Errorf("admin: status=%d", r.Status)
	}
}

func TestErrorMappingDoesNotLeakInternals(t *testing.T) {
	_, nc := startServer(t, testRouter())
	if r := nc.request(1, 1, msgFail, `"notfound"`); r.Status != StatusNotFound {
		t.Errorf("notfound: status=%d", r.Status)
	}
	if r := nc.request(1, 2, msgFail, `"conflict"`); r.Status != StatusConflict {
		t.Errorf("conflict: status=%d", r.Status)
	}
	r := nc.request(1, 3, msgFail, `"other"`)
	if r.Status != StatusInternal {
		t.Errorf("loi la: status=%d", r.Status)
	}
	if string(r.Payload) != `{"message":"internal server error"}` {
		t.Errorf("lo chi tiet noi bo cho client: %s", r.Payload)
	}
}

func TestPanicIsContained(t *testing.T) {
	_, nc := startServer(t, testRouter())
	if r := nc.request(1, 1, msgPanic, `{}`); r.Status != StatusInternal {
		t.Fatalf("panic: status=%d", r.Status)
	}
	// Server van song sau panic.
	if r := nc.request(1, 2, msgEcho, `{}`); r.Status != StatusOK {
		t.Fatalf("server chet sau panic: status=%d", r.Status)
	}
}

func TestRequestTimeout(t *testing.T) {
	_, nc := startServer(t, testRouter())
	if r := nc.request(1, 1, msgSlow, `{}`); r.Status != StatusTimeout {
		t.Fatalf("timeout: status=%d", r.Status)
	}
}

func TestPingPong(t *testing.T) {
	_, nc := startServer(t, testRouter())
	nc.send(&Frame{Header: Header{Kind: KindPing, ReqID: 99}, Payload: []byte("hi")})
	pong := nc.recv()
	if pong.Kind != KindPong || pong.ReqID != 99 || string(pong.Payload) != "hi" {
		t.Fatalf("pong sai: %+v %q", pong.Header, pong.Payload)
	}
}

func TestPipelinedRequestsAreAnsweredByReqID(t *testing.T) {
	_, nc := startServer(t, testRouter())
	const n = 50
	for i := 1; i <= n; i++ {
		nc.send(&Frame{Header: Header{Kind: KindRequest, Type: msgEcho, ConnID: 1, ReqID: uint64(i)}, Payload: []byte(`{}`)})
	}
	seen := map[uint64]bool{}
	for i := 0; i < n; i++ {
		seen[nc.recv().ReqID] = true
	}
	if len(seen) != n {
		t.Fatalf("nhan %d phan hoi khac nhau, muon %d", len(seen), n)
	}
}

func TestPushToUserAndBroadcast(t *testing.T) {
	srv, nc := startServer(t, testRouter())

	nc.send(&Frame{Header: Header{Kind: KindConnOpen, ConnID: 1}, Payload: []byte(`{"remote":"1.1.1.1:1","transport":"ws"}`)})
	nc.send(&Frame{Header: Header{Kind: KindConnOpen, ConnID: 2}, Payload: []byte(`{"remote":"2.2.2.2:2","transport":"tcp"}`)})
	nc.request(1, 1, msgLogin, `{"id":9,"role":"customer"}`)
	nc.request(2, 2, msgLogin, `{"id":9,"role":"customer"}`) // cung nguoi dung mo them mot tab
	nc.request(3, 3, msgLogin, `{"id":10,"role":"customer"}`)

	if got := srv.PushToUser(9, MsgType(0x6001), map[string]string{"m": "tre chuyen"}); got != 2 {
		t.Fatalf("PushToUser gui duoc %d, muon 2", got)
	}
	targets := map[uint32]bool{}
	for i := 0; i < 2; i++ {
		f := nc.recv()
		if f.Kind != KindPush || f.Type != MsgType(0x6001) {
			t.Fatalf("khung push sai: %+v", f.Header)
		}
		targets[f.ConnID] = true
	}
	if !targets[1] || !targets[2] {
		t.Fatalf("push phai toi conn 1 va 2, nhan %v", targets)
	}
	if got := srv.PushToUser(12345, MsgType(0x6001), nil); got != 0 {
		t.Fatalf("nguoi dung offline phai tra 0, nhan %d", got)
	}

	if err := srv.Broadcast(MsgType(0x6002), map[string]string{"m": "bao tri"}); err != nil {
		t.Fatal(err)
	}
	if f := nc.recv(); f.Kind != KindPush || f.ConnID != 0 {
		t.Fatalf("broadcast phai co conn_id=0: %+v", f.Header)
	}
}

func TestConnCloseDropsSession(t *testing.T) {
	srv, nc := startServer(t, testRouter())
	nc.request(1, 1, msgLogin, `{"id":9,"role":"customer"}`)
	if _, sessions := srv.Stats(); sessions != 1 {
		t.Fatalf("sessions = %d, muon 1", sessions)
	}
	nc.send(&Frame{Header: Header{Kind: KindConnClose, ConnID: 1}})
	// Ping/pong dam bao khung CONN_CLOSE truoc do da duoc xu ly (cung mot luong, tuan tu).
	nc.send(&Frame{Header: Header{Kind: KindPing}})
	nc.recv()
	if _, sessions := srv.Stats(); sessions != 0 {
		t.Fatalf("sessions = %d sau CONN_CLOSE, muon 0", sessions)
	}
	if got := srv.PushToUser(9, MsgType(0x6001), nil); got != 0 {
		t.Fatalf("khong duoc push toi phien da dong, gui duoc %d", got)
	}
}

func TestBadFrameClosesLink(t *testing.T) {
	_, nc := startServer(t, testRouter())
	_, _ = nc.conn.Write([]byte("GET / HTTP/1.1\r\nHost: x\r\n\r\n12345678")) // >= 24 byte, sai magic
	_ = nc.conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	if _, err := ReadFrame(nc.br); err == nil {
		t.Fatal("server phai dong ket noi khi nhan khung sai magic")
	}
}

func TestManyConnectionsLoginConcurrently(t *testing.T) {
	_, nc := startServer(t, testRouter())
	var wg sync.WaitGroup
	const n = 100
	for i := 1; i <= n; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			nc.send(&Frame{Header: Header{Kind: KindRequest, Type: msgLogin, ConnID: uint32(i), ReqID: uint64(i)}, Payload: []byte(`{"id":1,"role":"customer"}`)})
		}(i)
	}
	wg.Wait()
	for i := 0; i < n; i++ {
		if f := nc.recv(); f.Status != StatusOK {
			t.Fatalf("status=%d", f.Status)
		}
	}
}
