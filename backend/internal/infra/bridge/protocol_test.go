package bridge

import (
	"bytes"
	"errors"
	"io"
	"strings"
	"testing"
	"testing/iotest"
)

func sampleFrame() *Frame {
	return &Frame{
		Header:  Header{Kind: KindRequest, Type: MsgAuthLogin, Status: 0, ConnID: 7, ReqID: 0x0102030405060708},
		Payload: []byte(`{"email":"a@b.c","password":"x"}`),
	}
}

func TestEncodeDecodeRoundTrip(t *testing.T) {
	in := sampleFrame()
	buf, err := EncodeFrame(in)
	if err != nil {
		t.Fatal(err)
	}
	if len(buf) != HeaderSize+len(in.Payload) {
		t.Fatalf("do dai buffer = %d, muon %d", len(buf), HeaderSize+len(in.Payload))
	}
	// Kiem tra tung byte cua header theo dung dac ta (big-endian).
	want := []byte{0x4C, 0x54, 1, byte(KindRequest), 0x01, 0x02, 0, 0, 0, 0, 0, 7, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, byte(len(in.Payload))}
	if !bytes.Equal(buf[:HeaderSize], want) {
		t.Fatalf("header = % x\nmuon     % x", buf[:HeaderSize], want)
	}

	out, err := ReadFrame(bytes.NewReader(buf))
	if err != nil {
		t.Fatal(err)
	}
	if out.Header.Kind != in.Kind || out.Type != in.Type || out.ConnID != in.ConnID || out.ReqID != in.ReqID ||
		out.Length != uint32(len(in.Payload)) || !bytes.Equal(out.Payload, in.Payload) {
		t.Fatalf("doc lai khac ban dau: %+v", out)
	}
}

// TCP khong giu ranh gioi goi tin: doc tung byte mot van phai ghep dung.
func TestReadFrameOneByteAtATime(t *testing.T) {
	buf, _ := EncodeFrame(sampleFrame())
	out, err := ReadFrame(iotest.OneByteReader(bytes.NewReader(buf)))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(out.Payload, sampleFrame().Payload) {
		t.Fatal("payload sai khi doc tung byte")
	}
}

// Nhieu khung dinh lien nhau trong cung luong phai tach duoc dung tung khung.
func TestReadFrameBackToBack(t *testing.T) {
	var stream bytes.Buffer
	for i := 0; i < 3; i++ {
		f := sampleFrame()
		f.ReqID = uint64(i + 1)
		if err := WriteFrame(&stream, f); err != nil {
			t.Fatal(err)
		}
	}
	for i := 0; i < 3; i++ {
		f, err := ReadFrame(&stream)
		if err != nil {
			t.Fatal(err)
		}
		if f.ReqID != uint64(i+1) {
			t.Fatalf("khung %d co req_id=%d", i, f.ReqID)
		}
	}
	if _, err := ReadFrame(&stream); !errors.Is(err, io.EOF) {
		t.Fatalf("het luong phai tra io.EOF, nhan %v", err)
	}
}

func TestReadFrameErrors(t *testing.T) {
	good, _ := EncodeFrame(sampleFrame())

	badMagic := append([]byte(nil), good...)
	badMagic[0] = 0
	if _, err := ReadFrame(bytes.NewReader(badMagic)); !errors.Is(err, ErrBadMagic) {
		t.Errorf("sai magic: nhan %v", err)
	}

	badVersion := append([]byte(nil), good...)
	badVersion[2] = 99
	if _, err := ReadFrame(bytes.NewReader(badVersion)); !errors.Is(err, ErrBadVersion) {
		t.Errorf("sai version: nhan %v", err)
	}

	tooBig := append([]byte(nil), good[:HeaderSize]...)
	tooBig[20], tooBig[21], tooBig[22], tooBig[23] = 0xFF, 0xFF, 0xFF, 0xFF
	if _, err := ReadFrame(bytes.NewReader(tooBig)); !errors.Is(err, ErrPayloadTooLarge) {
		t.Errorf("payload qua lon: nhan %v", err)
	}

	if _, err := ReadFrame(bytes.NewReader(good[:10])); !errors.Is(err, io.ErrUnexpectedEOF) {
		t.Errorf("cat giua header: nhan %v", err)
	}
	if _, err := ReadFrame(bytes.NewReader(good[:len(good)-3])); !errors.Is(err, io.ErrUnexpectedEOF) {
		t.Errorf("cat giua payload: nhan %v", err)
	}
}

func TestEncodeFrameRejectsOversize(t *testing.T) {
	f := &Frame{Payload: make([]byte, MaxPayload+1)}
	if _, err := EncodeFrame(f); !errors.Is(err, ErrPayloadTooLarge) {
		t.Fatalf("nhan %v", err)
	}
}

func TestMaskJSON(t *testing.T) {
	got := MaskJSON([]byte(`{"email":"a@b.c","password":"secret","nested":{"Token":"tok123","ok":1},"list":[{"cvc":"987"}]}`))
	for _, leaked := range []string{"secret", "tok123", "987"} {
		if strings.Contains(got, leaked) {
			t.Errorf("log lo %q: %s", leaked, got)
		}
	}
	if !strings.Contains(got, "a@b.c") {
		t.Errorf("khong nen che email: %s", got)
	}
	if MaskJSON([]byte("khong phai json")) != "****" {
		t.Error("payload khong phai JSON phai bi che hoan toan")
	}
}
