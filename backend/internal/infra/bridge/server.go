package bridge

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"time"
)

// Config cua server kenh noi bo. Truong de trong se lay gia tri mac dinh.
type Config struct {
	Network        string        // "tcp" (mac dinh) hoac "unix"
	Address        string        // vd ":9100" hoac "/tmp/ltm-bridge.sock"
	MaxInFlight    int           // so yeu cau xu ly song song toi da tren moi duong noi (mac dinh 64)
	RequestTimeout time.Duration // han xu ly mot yeu cau (mac dinh 15s)
	WriteTimeout   time.Duration // han ghi mot khung (mac dinh 10s)
	Logger         FrameLogger   // mac dinh NopLogger
}

// Server lang nghe ket noi tu netcore (C), doc khung, goi handler va tra phan hoi.
// Thuong chi co mot ket noi tu netcore, nhung code ho tro nhieu (vd netcore khoi dong lai).
type Server struct {
	cfg    Config
	router *Router

	mu      sync.Mutex
	ln      net.Listener
	bridges map[*bridgeConn]struct{}
	wg      sync.WaitGroup
}

func NewServer(cfg Config, router *Router) *Server {
	if cfg.Network == "" {
		cfg.Network = "tcp"
	}
	if cfg.MaxInFlight <= 0 {
		cfg.MaxInFlight = 64
	}
	if cfg.RequestTimeout <= 0 {
		cfg.RequestTimeout = 15 * time.Second
	}
	if cfg.WriteTimeout <= 0 {
		cfg.WriteTimeout = 10 * time.Second
	}
	if cfg.Logger == nil {
		cfg.Logger = NopLogger{}
	}
	return &Server{cfg: cfg, router: router, bridges: make(map[*bridgeConn]struct{})}
}

// Addr tra ve dia chi dang lang nghe (nil neu chua Serve). Huu ich khi Address la ":0" trong test.
func (s *Server) Addr() net.Addr {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.ln == nil {
		return nil
	}
	return s.ln.Addr()
}

// ListenAndServe mo cong theo Config roi phuc vu cho den khi ctx bi huy.
func (s *Server) ListenAndServe(ctx context.Context) error {
	ln, err := net.Listen(s.cfg.Network, s.cfg.Address)
	if err != nil {
		return err
	}
	return s.Serve(ctx, ln)
}

// Serve nhan ket noi tu ln cho den khi ctx bi huy (tra nil) hoac listener loi (tra loi do).
func (s *Server) Serve(ctx context.Context, ln net.Listener) error {
	s.mu.Lock()
	s.ln = ln
	s.mu.Unlock()

	stop := make(chan struct{})
	defer close(stop)
	go func() {
		select {
		case <-ctx.Done():
			ln.Close()
		case <-stop:
		}
	}()

	for {
		conn, err := ln.Accept()
		if err != nil {
			if ctx.Err() != nil || errors.Is(err, net.ErrClosed) {
				break
			}
			// Loi tam thoi (het file descriptor...): nghi mot chut roi thu lai, tranh vong lap nong.
			time.Sleep(100 * time.Millisecond)
			continue
		}
		b := newBridgeConn(s, conn)
		s.addBridge(b)
		s.wg.Add(1)
		go func() {
			defer s.wg.Done()
			defer s.removeBridge(b)
			b.serve(ctx)
		}()
	}

	s.closeAllBridges()
	s.wg.Wait()
	return nil
}

func (s *Server) addBridge(b *bridgeConn) {
	s.mu.Lock()
	s.bridges[b] = struct{}{}
	s.mu.Unlock()
}

func (s *Server) removeBridge(b *bridgeConn) {
	s.mu.Lock()
	delete(s.bridges, b)
	s.mu.Unlock()
}

func (s *Server) closeAllBridges() {
	s.mu.Lock()
	list := make([]*bridgeConn, 0, len(s.bridges))
	for b := range s.bridges {
		list = append(list, b)
	}
	s.mu.Unlock()
	for _, b := range list {
		b.conn.Close()
	}
}

func (s *Server) snapshotBridges() []*bridgeConn {
	s.mu.Lock()
	defer s.mu.Unlock()
	list := make([]*bridgeConn, 0, len(s.bridges))
	for b := range s.bridges {
		list = append(list, b)
	}
	return list
}

// PushToUser day mot tin toi MOI ket noi dang dang nhap bang userID (mot nguoi co the mo nhieu tab).
// Tra ve so ket noi da gui. 0 nghia la nguoi do dang offline (thong bao van nam trong bang Notifications).
func (s *Server) PushToUser(userID int64, t MsgType, payload any) int {
	data, err := json.Marshal(payload)
	if err != nil {
		return 0
	}
	sent := 0
	for _, b := range s.snapshotBridges() {
		for _, sess := range b.snapshotSessions() {
			if id, _ := sess.Identity(); id == userID {
				f := &Frame{Header: Header{Kind: KindPush, Type: t, ConnID: sess.ConnID}, Payload: data}
				if b.send(f) == nil {
					b.logOut(sess, f, "")
					sent++
				}
			}
		}
	}
	return sent
}

// Broadcast day mot tin toi tat ca client (conn_id = 0: netcore tu gui cho moi ket noi).
func (s *Server) Broadcast(t MsgType, payload any) error {
	data, err := json.Marshal(payload)
	if err != nil {
		return err
	}
	var firstErr error
	for _, b := range s.snapshotBridges() {
		f := &Frame{Header: Header{Kind: KindPush, Type: t, ConnID: 0}, Payload: data}
		if err := b.send(f); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}

// Stats cho giam sat: so duong noi tu netcore va so client dang co.
func (s *Server) Stats() (bridges, sessions int) {
	list := s.snapshotBridges()
	for _, b := range list {
		sessions += len(b.snapshotSessions())
	}
	return len(list), sessions
}

// bridgeConn la mot ket noi TCP/Unix tu netcore toi Go.
type bridgeConn struct {
	srv    *Server
	conn   net.Conn
	remote string

	wmu sync.Mutex // chi cho mot goroutine ghi tai mot thoi diem de khung khong bi tron

	smu      sync.Mutex
	sessions map[uint32]*Session

	sem      chan struct{} // gioi han so handler chay song song
	handlers sync.WaitGroup
}

func newBridgeConn(s *Server, conn net.Conn) *bridgeConn {
	return &bridgeConn{
		srv:      s,
		conn:     conn,
		remote:   conn.RemoteAddr().String(),
		sessions: make(map[uint32]*Session),
		sem:      make(chan struct{}, s.cfg.MaxInFlight),
	}
}

func (b *bridgeConn) serve(ctx context.Context) {
	// Huy ctx khi duong noi dut de cac handler dang chay tu bo cuoc.
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	defer b.conn.Close()

	b.sysLog(0, "", "netcore connected")
	br := bufio.NewReaderSize(b.conn, 64<<10)
	for {
		f, err := ReadFrame(br)
		if err != nil {
			if !errors.Is(err, io.EOF) && !errors.Is(err, net.ErrClosed) && ctx.Err() == nil {
				b.sysLog(0, "", fmt.Sprintf("read error: %v", err))
			}
			break
		}
		b.dispatch(ctx, f)
	}
	cancel()
	b.handlers.Wait()
	b.sysLog(0, "", "netcore disconnected")
}

func (b *bridgeConn) dispatch(ctx context.Context, f *Frame) {
	switch f.Kind {
	case KindRequest:
		// Day la cho gioi han tai: het cho thi ngung doc, TCP se dan nguoc ap luc ve netcore.
		select {
		case b.sem <- struct{}{}:
		case <-ctx.Done():
			return
		}
		b.handlers.Add(1)
		go func() {
			defer b.handlers.Done()
			defer func() { <-b.sem }()
			b.handleRequest(ctx, f)
		}()

	case KindConnOpen:
		var info struct {
			Remote    string `json:"remote"`
			Transport string `json:"transport"`
		}
		_ = json.Unmarshal(f.Payload, &info)
		b.openSession(f.ConnID, info.Remote, info.Transport)
		b.sysLog(f.ConnID, info.Remote, "client connected via "+info.Transport)

	case KindConnClose:
		if s := b.closeSession(f.ConnID); s != nil {
			b.sysLog(f.ConnID, s.Remote, "client disconnected")
		}

	case KindPing:
		_ = b.send(&Frame{Header: Header{Kind: KindPong, ReqID: f.ReqID}, Payload: f.Payload})

	case KindPong:
		// khong can xu ly

	default:
		b.sysLog(f.ConnID, "", fmt.Sprintf("bo qua khung kind=%d", f.Kind))
	}
}

func (b *bridgeConn) handleRequest(ctx context.Context, f *Frame) {
	start := time.Now()
	sess := b.session(f.ConnID)
	uid, _ := sess.Identity()
	b.srv.cfg.Logger.LogFrame(FrameEvent{
		Dir: DirIn, Remote: sess.Remote, ConnID: f.ConnID, UserID: uid, Kind: f.Kind, Type: f.Type,
		ReqID: f.ReqID, Size: len(f.Payload), Payload: f.Payload,
	})

	resp := &Frame{Header: Header{Kind: KindResponse, Type: f.Type, ConnID: f.ConnID, ReqID: f.ReqID}}
	note := ""

	rt, ok := b.srv.router.routes[f.Type]
	switch {
	case !ok:
		failFrame(resp, StatusUnknownType, "unknown message type")
	case rt.auth >= AuthUser && !sess.IsAuthenticated():
		failFrame(resp, StatusUnauthorized, "authentication required")
	case rt.auth == AuthAdmin && !sess.IsAdmin():
		failFrame(resp, StatusForbidden, "admin privileges required")
	default:
		cctx, cancel := context.WithTimeout(ctx, b.srv.cfg.RequestTimeout)
		result, err := callHandler(cctx, rt.handler, sess, f.Payload)
		cancel()
		if err != nil {
			status, msg := StatusOf(err)
			failFrame(resp, status, msg)
			note = err.Error() // chi ghi vao log server, khong gui cho client
		} else if data, merr := marshalResult(result); merr != nil {
			failFrame(resp, StatusInternal, "internal server error")
			note = merr.Error()
		} else {
			resp.Payload = data
		}
	}

	// Danh tinh co the vua doi (dang nhap/dang xuat) nen doc lai de log.
	uid, _ = sess.Identity()
	if err := b.send(resp); err != nil {
		note = "send failed: " + err.Error()
	}
	b.srv.cfg.Logger.LogFrame(FrameEvent{
		Dir: DirOut, Remote: sess.Remote, ConnID: f.ConnID, UserID: uid, Kind: resp.Kind, Type: resp.Type,
		ReqID: resp.ReqID, Status: resp.Status, Size: len(resp.Payload), Elapsed: time.Since(start),
		Note: note, Payload: resp.Payload,
	})
}

// callHandler chay handler va bien panic thanh loi 500 de mot loi khong lam sap ca server.
func callHandler(ctx context.Context, h HandlerFunc, s *Session, payload []byte) (result any, err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("panic trong handler: %v", r)
		}
	}()
	return h(ctx, s, json.RawMessage(payload))
}

func marshalResult(v any) ([]byte, error) {
	if v == nil {
		return []byte("{}"), nil
	}
	data, err := json.Marshal(v)
	if err != nil {
		return nil, err
	}
	if len(data) > MaxPayload {
		return nil, ErrPayloadTooLarge
	}
	return data, nil
}

func failFrame(f *Frame, status uint16, msg string) {
	f.Status = status
	f.Payload, _ = json.Marshal(map[string]string{"message": msg})
}

// send ma hoa va ghi mot khung, co khoa ghi va han ghi. Ghi loi thi dong duong noi.
func (b *bridgeConn) send(f *Frame) error {
	buf, err := EncodeFrame(f)
	if err != nil {
		return err
	}
	b.wmu.Lock()
	defer b.wmu.Unlock()
	_ = b.conn.SetWriteDeadline(time.Now().Add(b.srv.cfg.WriteTimeout))
	if _, err := b.conn.Write(buf); err != nil {
		b.conn.Close()
		return err
	}
	return nil
}

func (b *bridgeConn) push(connID uint32, t MsgType, payload any) error {
	data, err := marshalResult(payload)
	if err != nil {
		return err
	}
	f := &Frame{Header: Header{Kind: KindPush, Type: t, ConnID: connID}, Payload: data}
	if err := b.send(f); err != nil {
		return err
	}
	b.logOut(b.session(connID), f, "")
	return nil
}

func (b *bridgeConn) logOut(s *Session, f *Frame, note string) {
	uid, _ := s.Identity()
	b.srv.cfg.Logger.LogFrame(FrameEvent{
		Dir: DirOut, Remote: s.Remote, ConnID: f.ConnID, UserID: uid, Kind: f.Kind, Type: f.Type,
		Size: len(f.Payload), Note: note, Payload: f.Payload,
	})
}

func (b *bridgeConn) sysLog(connID uint32, remote, note string) {
	if remote == "" {
		remote = b.remote
	}
	b.srv.cfg.Logger.LogFrame(FrameEvent{Dir: DirSys, Remote: remote, ConnID: connID, Note: note})
}

// session lay phien theo conn_id, tu tao neu netcore chua gui CONN_OPEN (de chiu loi hon).
func (b *bridgeConn) session(connID uint32) *Session {
	b.smu.Lock()
	defer b.smu.Unlock()
	s, ok := b.sessions[connID]
	if !ok {
		s = &Session{ConnID: connID, Remote: b.remote, bridge: b}
		b.sessions[connID] = s
	}
	return s
}

func (b *bridgeConn) openSession(connID uint32, remote, transport string) {
	b.smu.Lock()
	defer b.smu.Unlock()
	if remote == "" {
		remote = b.remote
	}
	b.sessions[connID] = &Session{ConnID: connID, Remote: remote, Transport: transport, bridge: b}
}

func (b *bridgeConn) closeSession(connID uint32) *Session {
	b.smu.Lock()
	defer b.smu.Unlock()
	s := b.sessions[connID]
	delete(b.sessions, connID)
	return s
}

func (b *bridgeConn) snapshotSessions() []*Session {
	b.smu.Lock()
	defer b.smu.Unlock()
	list := make([]*Session, 0, len(b.sessions))
	for _, s := range b.sessions {
		list = append(list, s)
	}
	return list
}
