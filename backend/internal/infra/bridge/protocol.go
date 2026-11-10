// Package bridge la tang giao tiep cua Go voi netcore (C) qua kenh noi bo ("bridge").
//
// netcore lo moi thu ve duong truyen (TCP, WebSocket, CRC, gui lai, heartbeat...) roi chuyen
// tung yeu cau nghiep vu sang day. Package nay thay cho tang HTTP (Gin) cua Qairlines:
// nhan khung, tim handler theo loai thong diep, goi usecase, tra khung phan hoi.
// Dac ta day du: docs/BRIDGE-PROTOCOL.md.
package bridge

import (
	"encoding/binary"
	"errors"
	"io"
)

const (
	// Magic dau moi khung: 0x4C54 = "LT".
	Magic uint16 = 0x4C54
	// Version cua giao thuc noi bo.
	Version uint8 = 1
	// HeaderSize la kich thuoc co dinh cua phan dau khung (byte).
	HeaderSize = 24
	// MaxPayload gioi han phan du lieu mot khung (1 MiB) de tranh cap phat bo nho theo y client.
	MaxPayload = 1 << 20
)

var (
	ErrBadMagic        = errors.New("bridge: sai magic, khung khong hop le")
	ErrBadVersion      = errors.New("bridge: phien ban giao thuc khong ho tro")
	ErrPayloadTooLarge = errors.New("bridge: payload vuot qua gioi han")
)

// Kind phan biet vai tro cua khung tren kenh noi bo.
type Kind uint8

const (
	KindRequest   Kind = 1 // C -> Go: yeu cau nghiep vu cua mot client
	KindResponse  Kind = 2 // Go -> C: phan hoi cho mot Request (cung conn_id, req_id)
	KindPush      Kind = 3 // Go -> C: tin day chu dong; conn_id = 0 la gui cho moi ket noi
	KindConnOpen  Kind = 4 // C -> Go: co client moi, payload JSON {"remote","transport"}
	KindConnClose Kind = 5 // C -> Go: client da ngat
	KindPing      Kind = 6 // hai chieu: kiem tra kenh con song
	KindPong      Kind = 7 // tra loi Ping, giu nguyen payload
	KindKick      Kind = 8 // Go -> C: yeu cau dong ket noi conn_id
)

func (k Kind) String() string {
	switch k {
	case KindRequest:
		return "REQUEST"
	case KindResponse:
		return "RESPONSE"
	case KindPush:
		return "PUSH"
	case KindConnOpen:
		return "CONN_OPEN"
	case KindConnClose:
		return "CONN_CLOSE"
	case KindPing:
		return "PING"
	case KindPong:
		return "PONG"
	case KindKick:
		return "KICK"
	}
	return "UNKNOWN"
}

// Header la phan dau 24 byte cua moi khung (big-endian):
//
//	0  magic   u16
//	2  version u8
//	3  kind    u8
//	4  type    u16   loai thong diep nghiep vu
//	6  status  u16   ma ket qua (chi co nghia trong Response)
//	8  conn_id u32   ket noi cua client phia netcore
//	12 req_id  u64   so yeu cau do client sinh, tra lai nguyen ven
//	20 length  u32   so byte payload
type Header struct {
	Kind   Kind
	Type   MsgType
	Status uint16
	ConnID uint32
	ReqID  uint64
	Length uint32
}

// Frame la mot khung hoan chinh: Header + Payload (JSON UTF-8).
type Frame struct {
	Header
	Payload []byte
}

// ReadFrame doc dung mot khung tu luong byte. TCP khong giu ranh gioi goi tin nen
// phai doc du header roi doc du Length byte; io.ReadFull lap cho den khi du.
// Tra io.EOF neu luong dong ngay tai ranh gioi khung, io.ErrUnexpectedEOF neu dong giua chung.
func ReadFrame(r io.Reader) (*Frame, error) {
	var hdr [HeaderSize]byte
	if _, err := io.ReadFull(r, hdr[:]); err != nil {
		return nil, err
	}
	if binary.BigEndian.Uint16(hdr[0:2]) != Magic {
		return nil, ErrBadMagic
	}
	if hdr[2] != Version {
		return nil, ErrBadVersion
	}

	f := &Frame{Header: Header{
		Kind:   Kind(hdr[3]),
		Type:   MsgType(binary.BigEndian.Uint16(hdr[4:6])),
		Status: binary.BigEndian.Uint16(hdr[6:8]),
		ConnID: binary.BigEndian.Uint32(hdr[8:12]),
		ReqID:  binary.BigEndian.Uint64(hdr[12:20]),
		Length: binary.BigEndian.Uint32(hdr[20:24]),
	}}
	if f.Length > MaxPayload {
		return nil, ErrPayloadTooLarge
	}
	if f.Length > 0 {
		f.Payload = make([]byte, f.Length)
		if _, err := io.ReadFull(r, f.Payload); err != nil {
			if errors.Is(err, io.EOF) {
				err = io.ErrUnexpectedEOF
			}
			return nil, err
		}
	}
	return f, nil
}

// EncodeFrame ghep header + payload thanh mot buffer de ghi bang mot lan Write.
// Length trong header luon lay theo len(Payload).
func EncodeFrame(f *Frame) ([]byte, error) {
	if len(f.Payload) > MaxPayload {
		return nil, ErrPayloadTooLarge
	}
	buf := make([]byte, HeaderSize+len(f.Payload))
	binary.BigEndian.PutUint16(buf[0:2], Magic)
	buf[2] = Version
	buf[3] = byte(f.Kind)
	binary.BigEndian.PutUint16(buf[4:6], uint16(f.Type))
	binary.BigEndian.PutUint16(buf[6:8], f.Status)
	binary.BigEndian.PutUint32(buf[8:12], f.ConnID)
	binary.BigEndian.PutUint64(buf[12:20], f.ReqID)
	binary.BigEndian.PutUint32(buf[20:24], uint32(len(f.Payload)))
	copy(buf[HeaderSize:], f.Payload)
	return buf, nil
}

// WriteFrame ma hoa va ghi mot khung. Goi tu nhieu goroutine phai tu khoa (xem bridgeConn.send).
func WriteFrame(w io.Writer, f *Frame) error {
	buf, err := EncodeFrame(f)
	if err != nil {
		return err
	}
	_, err = w.Write(buf)
	return err
}
