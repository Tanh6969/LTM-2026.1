package bridge

import "fmt"

// MsgType la ma loai thong diep nghiep vu (u16). Dang nhom theo byte cao:
// 0x01xx xac thuc, 0x02xx chuyen bay, 0x03xx dat ve, 0x04xx thanh toan, 0x05xx quan ly ve,
// 0x06xx thong bao, 0x07xx admin, 0x08xx giam sat. Bang day du trong docs/BRIDGE-PROTOCOL.md.
type MsgType uint16

const (
	// He thong
	MsgSysHealth MsgType = 0x0001

	// Xac thuc (giai doan 4)
	MsgAuthRegister MsgType = 0x0101
	MsgAuthLogin    MsgType = 0x0102
	MsgAuthLogout   MsgType = 0x0103
	MsgAuthToken    MsgType = 0x0104 // gan lai danh tinh cho ket noi moi bang token da co

	// Chuyen bay (giai doan 5)
	MsgFlightSearch MsgType = 0x0201
	MsgFlightGet    MsgType = 0x0202
	MsgFlightList   MsgType = 0x0203
)

var msgNames = map[MsgType]string{
	MsgSysHealth:    "SYS_HEALTH",
	MsgAuthRegister: "AUTH_REGISTER",
	MsgAuthLogin:    "AUTH_LOGIN",
	MsgAuthLogout:   "AUTH_LOGOUT",
	MsgAuthToken:    "AUTH_TOKEN",
	MsgFlightSearch: "FLIGHT_SEARCH",
	MsgFlightGet:    "FLIGHT_GET",
	MsgFlightList:   "FLIGHT_LIST",
}

// String tra ten de doc cho log; loai chua biet in dang 0xNNNN.
func (t MsgType) String() string {
	if name, ok := msgNames[t]; ok {
		return name
	}
	return fmt.Sprintf("0x%04X", uint16(t))
}

// Ma ket qua trong Response (u16). 0 = thanh cong, con lai muon so cua HTTP cho de nho.
const (
	StatusOK           uint16 = 0
	StatusBadRequest   uint16 = 400
	StatusUnauthorized uint16 = 401
	StatusForbidden    uint16 = 403
	StatusNotFound     uint16 = 404
	StatusConflict     uint16 = 409
	StatusInternal     uint16 = 500
	StatusUnknownType  uint16 = 501
	StatusUnavailable  uint16 = 503
	StatusTimeout      uint16 = 504
)
