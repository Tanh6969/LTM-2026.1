package bridge

import (
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"sync"
	"time"
)

// Huong cua mot dong log: IN = vao Go, OUT = ra khoi Go, SYS = su kien he thong.
const (
	DirIn  = "IN"
	DirOut = "OUT"
	DirSys = "SYS"
)

// FrameEvent la mot su kien can ghi log.
type FrameEvent struct {
	Time    time.Time
	Dir     string
	Remote  string
	ConnID  uint32
	UserID  int64
	Kind    Kind
	Type    MsgType
	ReqID   uint64
	Status  uint16
	Size    int
	Elapsed time.Duration
	Note    string
	Payload []byte // chi dung khi bat ghi noi dung
}

// FrameLogger nhan moi su kien gui/nhan tren kenh noi bo.
type FrameLogger interface {
	LogFrame(e FrameEvent)
}

// NopLogger bo qua moi log (dung trong test).
type NopLogger struct{}

func (NopLogger) LogFrame(FrameEvent) {}

// TextLogger ghi moi su kien thanh mot dong van ban, vi du:
//
//	2026-11-10T09:15:02.113Z IN  conn=17 10.0.0.5:52811 user=-  REQUEST  FLIGHT_SEARCH req=7 412B
//	2026-11-10T09:15:02.140Z OUT conn=17 10.0.0.5:52811 user=-  RESPONSE FLIGHT_SEARCH req=7 status=0 6203B 27ms
type TextLogger struct {
	mu         sync.Mutex
	w          io.Writer
	logPayload bool
}

// NewTextLogger tao logger ghi vao w. logPayload=true se ghi them noi dung (da che du lieu nhay cam).
func NewTextLogger(w io.Writer, logPayload bool) *TextLogger {
	return &TextLogger{w: w, logPayload: logPayload}
}

func (l *TextLogger) LogFrame(e FrameEvent) {
	if e.Time.IsZero() {
		e.Time = time.Now()
	}
	user := "-"
	if e.UserID != 0 {
		user = fmt.Sprintf("%d", e.UserID)
	}

	var b strings.Builder
	fmt.Fprintf(&b, "%s %-3s conn=%d %s user=%s", e.Time.UTC().Format("2006-01-02T15:04:05.000Z"), e.Dir, e.ConnID, e.Remote, user)
	if e.Kind != 0 {
		fmt.Fprintf(&b, " %s", e.Kind)
	}
	if e.Type != 0 {
		fmt.Fprintf(&b, " %s", e.Type)
	}
	if e.ReqID != 0 {
		fmt.Fprintf(&b, " req=%d", e.ReqID)
	}
	if e.Kind == KindResponse {
		fmt.Fprintf(&b, " status=%d", e.Status)
	}
	if e.Size > 0 {
		fmt.Fprintf(&b, " %dB", e.Size)
	}
	if e.Elapsed > 0 {
		fmt.Fprintf(&b, " %dms", e.Elapsed.Milliseconds())
	}
	if e.Note != "" {
		fmt.Fprintf(&b, " note=%q", e.Note)
	}
	if l.logPayload && len(e.Payload) > 0 {
		fmt.Fprintf(&b, " payload=%s", truncate(MaskJSON(e.Payload), 300))
	}
	b.WriteByte('\n')

	l.mu.Lock()
	_, _ = io.WriteString(l.w, b.String())
	l.mu.Unlock()
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "...(cat bot)"
}

// Cac khoa JSON bi che gia tri khi ghi log.
var sensitiveKeys = map[string]bool{
	"password": true, "oldpassword": true, "newpassword": true,
	"token": true, "accesstoken": true,
	"cardnumber": true, "cvc": true, "cvv": true, "clientsecret": true,
	"identificationnumber": true, "passportnumber": true,
}

// MaskJSON thay gia tri cua cac khoa nhay cam (mat khau, token, so the, CCCD...) bang "****".
// Neu payload khong phai JSON hop le thi che toan bo de khong lo du lieu.
func MaskJSON(raw []byte) string {
	var v any
	if err := json.Unmarshal(raw, &v); err != nil {
		return "****"
	}
	out, err := json.Marshal(maskValue(v))
	if err != nil {
		return "****"
	}
	return string(out)
}

func maskValue(v any) any {
	switch t := v.(type) {
	case map[string]any:
		for k, val := range t {
			if sensitiveKeys[strings.ToLower(k)] {
				t[k] = "****"
				continue
			}
			t[k] = maskValue(val)
		}
		return t
	case []any:
		for i := range t {
			t[i] = maskValue(t[i])
		}
		return t
	}
	return v
}
