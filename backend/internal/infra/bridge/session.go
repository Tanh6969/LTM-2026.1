package bridge

import "sync"

// Session la trang thai cua MOT ket noi client (conn_id cua netcore): da dang nhap chua, la ai.
// netcore khong biet gi ve nguoi dung; Go giu anh xa nay de biet day tin cho ai.
type Session struct {
	ConnID    uint32
	Remote    string // dia chi client, vd "10.0.0.5:51322"
	Transport string // "tcp" hoac "ws"

	bridge *bridgeConn

	mu     sync.RWMutex
	userID int64
	role   string
}

// Identity tra ve (userID, role); userID = 0 nghia la chua dang nhap (khach vang lai).
func (s *Session) Identity() (userID int64, role string) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.userID, s.role
}

// SetIdentity gan danh tinh sau khi dang nhap thanh cong.
func (s *Session) SetIdentity(userID int64, role string) {
	s.mu.Lock()
	s.userID, s.role = userID, role
	s.mu.Unlock()
}

// ClearIdentity quay ve khach vang lai (dang xuat).
func (s *Session) ClearIdentity() { s.SetIdentity(0, "") }

func (s *Session) IsAuthenticated() bool {
	id, _ := s.Identity()
	return id != 0
}

func (s *Session) IsAdmin() bool {
	id, role := s.Identity()
	return id != 0 && role == "admin"
}

// Push gui mot tin chu dong toi dung client nay (khong can req_id).
func (s *Session) Push(t MsgType, payload any) error {
	return s.bridge.push(s.ConnID, t, payload)
}

// Kick yeu cau netcore dong ket noi nay.
func (s *Session) Kick() error {
	return s.bridge.send(&Frame{Header: Header{Kind: KindKick, ConnID: s.ConnID}})
}
