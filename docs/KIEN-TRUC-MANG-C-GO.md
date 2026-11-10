# Kiến trúc mạng: C (tầng mạng) + Go (nghiệp vụ)

> Tài liệu để **tự học và chuẩn bị bảo vệ**. Đây là bản đề xuất, chưa có code. Phần mạng bằng C do bạn tự viết; tài liệu này chỉ mô tả cần làm gì, theo thứ tự nào và kiểm tra ra sao.

---

## 1. Nguyên tắc chia việc

> **C quyết định mọi thứ về "gói tin đi như thế nào". Go quyết định mọi thứ về "gói tin có nghĩa gì".**

| | C (`netcore/`) | Go (`backend/`) |
|---|---|---|
| Kết nối | accept, quản lý nhiều client, timeout, heartbeat | không biết có bao nhiêu client |
| Đóng gói | tách gói từ luồng TCP, header, CRC, seq/ack | không đụng tới |
| Tin cậy | phát hiện lỗi, timeout, gửi lại, chống xử lý trùng, nối lại phiên | chỉ cần idempotent làm lớp bảo vệ cuối |
| WebSocket | tự bắt tay (SHA-1/Base64), tự đọc/ghi frame | không biết |
| Đẩy tin | biết user nào đang ở kết nối nào, gửi push, giữ hàng đợi chưa ack | quyết định *gửi gì cho ai* (trễ chuyến, vé...) |
| Log | ghi **từng gói** vào/ra, kèm trạng thái CRC/seq | log nghiệp vụ |
| Nghiệp vụ | **không** hiểu JSON, chỉ chuyển tiếp | usecase, Postgres, email, Stripe, scheduler |

Điểm mấu chốt: C chỉ đọc **header**, còn payload (JSON) là hộp đen chuyển cho Go. Nhờ vậy ranh giới sạch và dễ bảo vệ.

### Về lo ngại "web socket chỉ có vài dòng"

WebSocket ở đây **chỉ là phương tiện chở byte** cho trình duyệt, vì trình duyệt không mở được TCP thô. Cách làm khiến nó "vài dòng" là dùng thư viện như socket.io, thứ tự lo hết mọi thứ. Thiết kế này tránh điều đó:

- Bạn **tự cài** bắt tay WebSocket và frame trong C (vài trăm dòng, gồm SHA-1 và Base64).
- Bên trong mỗi WebSocket message là **giao thức riêng của bạn** (header, CRC, seq/ack, retry). Mọi quyết định độ tin cậy nằm ở C và client, không nằm ở tầng web.
- Có thêm **client console bằng C chạy TCP thô** để chứng minh giao thức độc lập với web.

### Quyết định đã chốt

- **C** làm toàn bộ tầng mạng. **Go** giữ domain, usecase, Postgres, mail, Stripe, scheduler.
- **Không** viết nghiệp vụ và truy vấn DB bằng C (tránh `libpq`, parse JSON, SMTP, Stripe, bcrypt/JWT bằng C). Lý do: những phần đó nặng nhưng không giúp lấy điểm lập trình mạng; thầy cũng nhận xét full C là rất khó.
- **Không** có `go-helper/` riêng. Go đã có trách nhiệm cụ thể là toàn bộ nghiệp vụ và DB.
- Thư mục C đặt tên `backend/netcore/` (bản điều chỉnh của `c-server/` ban đầu: bỏ `application/` và `storage/`).

### Nhiều hãng bay (2–3 hãng theo yêu cầu của thầy)

Hãng bay là **dữ liệu, không phải kiến trúc**. Hệ thống đóng vai đại lý bán vé (như Traveloka, Skyscanner): một server, một luồng đặt vé, một luồng thanh toán, bán vé của nhiều hãng. **Không** mỗi hãng một server.

| Phần | Có đổi theo hãng không? |
|---|---|
| Đặt vé | Không. Một đơn có thể gồm chuyến đi hãng A và chuyến về hãng B (`departure_flight_id`, `return_flight_id`) |
| Thanh toán | Không. Khách trả một lần cho cả đơn. Chia tiền theo hãng chỉ là báo cáo (`RevenueByAirline`) |
| Tìm kiếm và so sánh giá/thời gian | Đây chính là chức năng so sánh các hãng: `SearchFlights` đã trả về hãng, giá, thời lượng |
| Quy định phí đổi/hủy, hành lý | Khác nhau theo hãng, nên lưu thành **cột hoặc bảng chính sách**, không viết code riêng từng hãng. Cần chốt chính sách trước (xem TONG-QUAN-DU-AN, câu hỏi mở số 5) |

DB đã sẵn sàng: `Airlines`, `Flights.airline_id`, `FlightFares` và seed 4 hãng có trong `backend/db/full_setup.sql`.

**Tính năng nâng cao tùy chọn (làm sau cùng):** "server hãng giả". Server chính gọi song song 2–3 server giả lập của từng hãng qua TCP, có timeout; hãng nào chậm hoặc lỗi thì bỏ qua và vẫn trả kết quả của các hãng còn lại. Phần này thể hiện lập trình mạng khá tốt (gọi song song, timeout, chịu lỗi từng phần) nhưng chỉ làm sau khi hệ thống chính chạy ổn.

---

## 2. Kiến trúc

```mermaid
flowchart LR
    subgraph Client
      B[Trình duyệt React<br/>NetClient JS]
      CC[Client console C<br/>TCP thô]
      MON[Monitor CLI C]
    end
    subgraph netcore["netcore (C) — tầng mạng"]
      TCP[Cổng TCP thô :9000]
      WS[Cổng WebSocket :9001]
      CORE[Conn manager · Framing · CRC<br/>seq/ack · retry · dedupe<br/>heartbeat · session · push · log]
      BR[Bridge client]
    end
    subgraph go["backend (Go) — nghiệp vụ"]
      SRV[infra/bridge<br/>listener nội bộ]
      UC[usecase · domain]
      DB[(Postgres)]
      EXT[mail · stripe · scheduler]
    end
    B -- WS binary --> WS
    CC -- TCP --> TCP
    MON -- TCP admin --> TCP
    TCP --> CORE
    WS --> CORE
    CORE --> BR
    BR -- Unix socket / TCP localhost<br/>cùng định dạng frame --> SRV
    SRV --> UC --> DB
    UC --> EXT
    UC -- NOTIFY_* --> SRV
```

- `netcore` thay thế vai trò `infra/api` (Gin) của Qairlines. `usecase`, `domain`, `postgresql`, `mail`, `stripe`, `worker` giữ nguyên bên Go.
- Go bên trong chỉ cần thêm một listener nội bộ (`infra/bridge`) nói chuyện với C.

---

## 3. Giao thức (đặt ra trước khi code)

### 3.1 Header nhị phân, big-endian, 24 byte

| Offset | Size | Trường | Ý nghĩa |
|---:|---:|---|---|
| 0 | 2 | `magic` | `0x4C54` ("LT"). Sai magic = rác hoặc lệch gói, đóng kết nối |
| 2 | 1 | `version` | phiên bản giao thức |
| 3 | 1 | `flags` | bit0 `ACK_REQ`, bit1 `IS_ACK`, bit2 `PUSH`, bit3 `RETRANS`, bit4 `IS_NACK` |
| 4 | 2 | `type` | loại gói (xem 3.2) |
| 6 | 2 | `reserved` | để dành |
| 8 | 4 | `seq` | số thứ tự gói của người gửi, tăng dần theo kết nối/phiên |
| 12 | 4 | `ack` | số `seq` cao nhất của đối phương mà mình đã nhận liên tục |
| 16 | 4 | `length` | độ dài payload (tối đa, ví dụ, 1 MiB; vượt = đóng kết nối) |
| 20 | 4 | `crc32` | CRC32 của header (trừ chính trường này) + payload |

`req_id` của yêu cầu (UUID hoặc số 64-bit do client sinh) đặt ở **đầu payload hoặc thêm một trường 8 byte vào header**. Quyết định này cần chốt sớm vì nó là khóa chống xử lý trùng.

Payload là JSON UTF-8 (Go parse dễ). Nếu còn thời gian có thể đổi sang nhị phân, nhưng không cần cho điểm.

### 3.2 Loại gói

- Điều khiển (do C xử lý, không chuyển cho Go): `HELLO`, `HELLO_OK` (cấp `session_id`), `RESUME`, `PING`, `PONG`, `ACK`, `NACK`, `BYE`.
- Nghiệp vụ (C chỉ định tuyến, Go xử lý): `AUTH_*`, `FLIGHT_SEARCH`, `SEAT_HOLD`, `BOOKING_*`, `PAYMENT_*`, `TICKET_*`, `ADMIN_*`.
- Push từ Go: `NOTIFY_*`, `SEAT_MAP_CHANGED`.
- Giám sát: `MON_*` (chỉ cổng admin).

---

## 4. "Gói tin đủ không, lỗi thì gửi lại thế nào"

### 4.1 TCP đã lo gì và còn thiếu gì

TCP tự gửi lại các segment bị mất và giữ đúng thứ tự byte. Nhưng khi bảo vệ, thầy sẽ muốn thấy bạn hiểu **những gì TCP không lo**:

| Vấn đề | Vì sao xảy ra | Cách xử lý của mình |
|---|---|---|
| **Ranh giới gói** | TCP là luồng byte: một `recv()` có thể trả nửa gói, hoặc 2,5 gói dính nhau | Bộ đệm nhận mỗi kết nối + máy trạng thái đọc header rồi đọc đủ `length` byte |
| **Ghi dở** | `send()` non-blocking có thể ghi được một phần | Hàng đợi ghi mỗi kết nối, ghi tiếp khi epoll báo `EPOLLOUT` |
| **Không biết server đã xử lý chưa** | Mất mạng sau khi gửi: client không biết yêu cầu đã chạy hay chưa | Client giữ bảng yêu cầu chờ trả lời, **timeout rồi gửi lại cùng `req_id`** |
| **Gửi lại gây xử lý trùng** (đặt vé 2 lần, trừ tiền 2 lần) | Hệ quả của dòng trên | Server nhớ `(session, req_id) → response`, gặp lại thì trả bản lưu, **không chạy lại** |
| **Kết nối chết im lặng** | Rớt wifi không có FIN, server cứ tưởng client còn | `PING/PONG` + timeout, quá hạn thì đóng, nhả ghế đang giữ |
| **Dữ liệu hỏng ở tầng ứng dụng** | Lỗi phần mềm, proxy trung gian, bug đóng gói | CRC32; sai thì bỏ gói và gửi `NACK` hoặc đóng kết nối |
| **Mất tin đẩy khi client offline** | Push trễ chuyến lúc client đang rớt mạng | Hàng đợi push chưa ack theo phiên + `RESUME` phát lại + lưu bảng `Notifications` |

### 4.2 Gửi lại và chống trùng (luồng điển hình)

```mermaid
sequenceDiagram
    participant C as Client
    participant N as netcore (C)
    participant G as Go
    C->>N: BOOKING_CREATE seq=7 req_id=R1
    N->>G: chuyển tiếp
    G-->>N: BOOKING_RES (đã ghi DB)
    N--xC: phản hồi bị mất (mạng rớt)
    Note over C: timeout 3s, chưa có phản hồi
    C->>N: BOOKING_CREATE seq=8 req_id=R1 flags=RETRANS
    Note over N: Tra bảng dedupe thấy R1 đã xử lý
    N-->>C: BOOKING_RES (bản lưu), không gọi Go lần nữa
```

Quy tắc phía client:
- Mỗi yêu cầu có `req_id` mới; gửi lại thì **giữ nguyên `req_id`**.
- Timeout tăng dần (ví dụ 2s, 4s, 8s). Quá 3 lần thì báo lỗi hoặc kết nối lại.
- Chỉ xóa yêu cầu khỏi bảng chờ khi nhận phản hồi khớp `req_id`.

Quy tắc phía server:
- Bảng dedupe có giới hạn kích thước và thời gian sống (ví dụ 5 phút, tối đa N mục mỗi phiên).
- Lớp bảo vệ cuối ở Go: ràng buộc UNIQUE trong DB và idempotency key khi gọi Stripe.

### 4.3 Nối lại phiên và đảm bảo thông báo

- Kết nối đầu tiên: `HELLO` → server trả `session_id`.
- Rớt mạng rồi kết nối lại: client gửi `RESUME{session_id, last_seq_received}`. Server phát lại các push chưa được ack từ vòng đệm của phiên.
- Mỗi push có `seq`; client ack. Client lọc trùng theo `seq`. Kết quả: ít nhất một lần ở tầng truyền, **đúng một lần** ở tầng hiển thị.
- Nếu phiên đã hết hạn (client offline quá lâu), Go vẫn có `Notifications` (`is_read=false`) để gửi khi đăng nhập lại.
- Ghế đang giữ gắn với phiên: nếu phiên chết hẳn (quá hạn), nhả ghế. Đây là chỗ nối tự nhiên giữa tầng mạng và `HoldSeat`/`ReleaseExpiredSeatHolds`.

### 4.4 Chứng minh được cho thầy xem (kịch bản demo)

Thầy cần thấy bằng mắt, nên phải có công cụ **gây lỗi có chủ đích**:

1. **Gói bị cắt đôi**: client console gửi một gói theo từng mẩu nhỏ, có độ trễ giữa các mẩu → server vẫn ghép đủ, log thấy nhiều lần `recv` nhưng một gói.
2. **Mất phản hồi**: proxy lỗi chặn phản hồi → client timeout, gửi lại → server báo "duplicate req_id", **DB chỉ có một đơn**.
3. **Gói hỏng**: proxy đổi 1 byte → CRC sai → log `CRC_FAIL`, gửi `NACK`, client gửi lại, thành công.
4. **Chết im lặng**: rút mạng client → quá hạn heartbeat → log `HEARTBEAT_TIMEOUT`, ghế được nhả.
5. **Nối lại**: ngắt rồi kết nối lại → `RESUME`, nhận đủ thông báo trễ chuyến đã bỏ lỡ.
6. **Tải**: 100–500 client giả lập (script hoặc client C) → đồng hồ đo của Monitor không rò kết nối/bộ nhớ.

**Công cụ gây lỗi** (viết bằng C, đây là một deliverable đáng giá): một **lossy proxy** đứng giữa client và server, tham số `drop%`, `delay_ms`, `corrupt%`, `dup%`, thao tác ở mức **frame**. Lưu ý trung thực: qua TCP thật, kernel che mất việc mất gói, nên proxy mô phỏng ở mức gói của ứng dụng. Muốn thấy mất gói thật ở mức mạng có thể dùng `tc netem` trong WSL/Linux.

### 4.5 Phần nâng cao tùy chọn: độ tin cậy trên UDP

Nếu muốn một chức năng thể hiện **retransmit thực sự** (stop-and-wait hoặc cửa sổ trượt):
- Một module UDP riêng, ví dụ gửi **file vé điện tử** (chia mảnh, `seq`, `ack`, timeout, gửi lại mảnh thiếu, CRC cuối file) hoặc khám phá server/thống kê (`UDP discovery`).
- Bản nhẹ hơn: tải file qua TCP theo mảnh có `offset` + CRC mỗi mảnh + tiếp tục từ chỗ dừng.
- Chỉ làm sau khi phần chính chạy ổn.

### 4.6 Hai bậc độ tin cậy

Bộ cơ chế truyền file (cửa sổ trượt, ACK tích lũy, RTO, fast retransmit, PROBE khi cửa sổ bằng 0, CRC từng mảnh, kiểm tra hash cuối file) của bài HW7 **không** áp dụng cho từng lệnh đặt vé vì quá nặng. Chia hai bậc:

| Bậc | Áp dụng cho | Cơ chế |
|---|---|---|
| 1 | Mọi thông điệp nghiệp vụ (`FLIGHT_SEARCH`, `SEAT_HOLD`, `BOOKING_CREATE`, `PAYMENT_*`, ...) | framing, CRC, `req_id`, timeout + gửi lại, chống xử lý trùng, heartbeat, `RESUME` |
| 2 | Truyền file (tải **vé điện tử PDF**) | tái sử dụng nền HW7: mảnh 4096 byte, cửa sổ trượt, ACK tích lũy, RTO, fast retransmit, PROBE, CRC + hash cuối file |

Cần phân biệt hai loại xác nhận:
- **ACK** xác nhận frame đã nhận đủ và đúng (tầng truyền).
- **Phản hồi nghiệp vụ** (ví dụ `BOOKING_CREATE_RES`) cho biết thao tác thành công hay thất bại (tầng ứng dụng).

Bậc 2 là chỗ khoe retransmit thật sự khi bảo vệ, và dùng lại được code đã làm ở HW7.

---

## 5. Thiết kế bên trong netcore (C)

### 5.1 Mô hình đồng thời
- Một luồng, **non-blocking socket + epoll** (khuyến nghị) là chuẩn của môn mạng. Cách khác: thread pool, hoặc `select/poll` nếu cần chạy trên Windows.
- Mỗi kết nối có một struct: `fd`, trạng thái (`HANDSHAKE_WS`, `OPEN`, `CLOSING`), bộ đệm nhận, hàng đợi ghi, `session_id`, `next_seq`, `last_ack`, bảng yêu cầu chờ, thời điểm hoạt động cuối.
- Bộ đếm thời gian (`timerfd` hoặc min-heap): heartbeat, timeout gửi lại, hết hạn phiên.

### 5.2 Các module (gợi ý)

Cấu trúc thư mục chốt (so với bản `c-server/` ban đầu: **bỏ `application/` và `storage/`**, thêm `session/` và `bridge/`; **bỏ `go-helper/`**):

```
backend/
├── netcore/                    # C: toàn bộ tầng mạng
│   ├── include/                # header dùng chung: frame, connection, protocol
│   ├── src/
│   │   ├── main.c              # khởi động, vòng lặp epoll
│   │   ├── transport/          # TCP, accept, gửi/nhận, quản lý kết nối, hàng đợi ghi
│   │   ├── protocol/           # frame, encode/decode, CRC32, loại thông điệp
│   │   ├── reliability/        # bậc 1: req_id, retry, dedupe, heartbeat
│   │   │                       # bậc 2: cửa sổ trượt, ACK, RTO, fast retransmit, PROBE (truyền file)
│   │   ├── websocket/          # bắt tay + frame RFC 6455, sha1, base64
│   │   ├── session/            # bảng phiên, RESUME, hàng đợi push chưa ack
│   │   ├── bridge/             # kết nối nội bộ tới Go, định tuyến phản hồi và push
│   │   ├── logging/            # log từng gói, CRC, retransmit, lỗi, thống kê, che dữ liệu nhạy cảm
│   │   └── config/             # cổng, giới hạn buffer, tham số chạy
│   ├── client/                 # client console TCP thô (bản tham chiếu của giao thức)
│   ├── tools/                  # lossy proxy (loss/corrupt/ACK-loss), load generator
│   ├── tests/                  # test frame, CRC, cửa sổ, RTO, dedupe
│   └── Makefile
├── internal/                   # Go (giữ nguyên): domain, usecase, postgresql, mail, stripe, worker
│   └── infra/bridge/           # listener nội bộ nhận từ C (thay infra/api của Gin)
└── db/                         # schema, migration, query (đã có)

frontend/src/
├── net/
│   ├── socketClient.js         # kết nối WebSocket, encode/decode frame, retry/timeout/RESUME
│   └── protocol.js             # các type và cấu trúc message dùng ở browser
└── services/                   # service nghiệp vụ gọi qua socketClient (thay các lời gọi REST)
```

`pages/`, `components/`, `hooks/` chỉ lo giao diện và trạng thái UI, không tự triển khai cơ chế mạng. `services/customerService.js` và các hook liên quan hiện còn gọi REST cũ, cần đổi sang gửi message qua `socketClient`.

#### Ước lượng quy mô (không có file nghìn dòng)

| Module | Dòng ước tính |
|---|---:|
| `protocol` (frame + crc32) | 200–300 |
| `transport` (conn + epoll) | 400–500 |
| `reliability` bậc 1 (req_id/retry/dedupe) | 400–600 |
| `reliability` bậc 2 (truyền file, tái sử dụng HW7) | 400–600 |
| `websocket` (+ sha1, base64) | 400–500 |
| `session` + push | 200–300 |
| `logging`, timer, admin | 300–400 |
| `bridge` | 250–350 |
| `main`, `config` | 100–200 |

Tổng khoảng **3.000–3.800 dòng trong 12–15 file, mỗi file 100–500 dòng**. Đủ nhỏ để tự gõ tay và hiểu từng dòng. Mẹo học: viết xong một module, đóng file và chép lại từ trí nhớ để kiểm tra có hiểu thật không.

### 5.3 Cầu nối với Go (`bridge.c` ↔ `infra/bridge`)
> **Đã triển khai ở giai đoạn 3** — đặc tả đầy đủ trong [BRIDGE-PROTOCOL.md](BRIDGE-PROTOCOL.md). Điểm khác so với dự kiến ban đầu: **Go (không phải C) giữ ánh xạ `conn_id` → người dùng**, nên C không cần biết user.

- Khung nội bộ riêng (header 24 byte + JSON, không có CRC/seq/ack vì đường cục bộ), chạy trên TCP localhost (mặc định `:9100`) hoặc Unix socket. C dùng thư viện `src/bridge/` để đọc/ghi khung.
- netcore gán `conn_id` cho mỗi client, gửi `CONN_OPEN` / `REQUEST` / `CONN_CLOSE`. Go xử lý và trả `RESPONSE` cùng `conn_id`, `req_id`.
- Đăng nhập thành công thì Go gắn danh tính vào `conn_id` đó (trong `Session`). Token PASETO do Go cấp và kiểm tra.
- Go muốn đẩy thông báo: tự tìm các `conn_id` của người dùng rồi gửi `PUSH` cho từng `conn_id` (hoặc `conn_id = 0` để broadcast). C chỉ cần chuyển tiếp theo `conn_id`, gắn `seq` của giao thức client, giữ trong hàng đợi chưa ack rồi gửi.
- Nếu Go chết/khởi động lại: C giữ client, trả lỗi tạm thời `503`, kết nối lại Go theo backoff.

### 5.4 Log
Mỗi gói ghi một dòng, ví dụ:
```
2026-10-28T09:15:02.417Z IN  conn=12 10.0.0.5:51322 type=BOOKING_CREATE seq=7 ack=3 len=412 crc=OK flags=ACK_REQ
2026-10-28T09:15:05.420Z IN  conn=12 type=BOOKING_CREATE seq=8 len=412 crc=OK flags=RETRANS dedupe=HIT req_id=R1
2026-10-28T09:15:05.421Z OUT conn=12 type=BOOKING_RES seq=5 len=188 (from cache)
2026-10-28T09:15:30.000Z SYS conn=12 HEARTBEAT_TIMEOUT idle=30s -> close, release seats
```
- Không ghi mật khẩu, số thẻ, token đầy đủ (che bằng `****`).
- Ghi file xoay vòng + giữ vòng đệm trong RAM cho `MON_TAIL`.

### 5.5 Giám sát (`MON_*`, cổng admin)
Số liệu: số kết nối, byte vào/ra, gói vào/ra, **số lần gửi lại, trùng bị chặn, CRC lỗi, timeout heartbeat**, độ trễ bridge → Go. Hiển thị bằng client console C (bảng tự làm mới) và/hoặc trang admin.

---

## 6. Phía trình duyệt

- Trình duyệt dùng `WebSocket` với `binaryType = "arraybuffer"`, mỗi message là một frame của giao thức mình.
- Viết một lớp `NetClient` (JS thuần, `DataView`) **cài lại cùng quy tắc** seq/ack/retry/timeout/RESUME. Spec nằm ở mục 3–4; bản tham chiếu là client console C.
- React chỉ gọi `net.request(type, payload)` (trả Promise) và `net.on("NOTIFY_*", ...)`. Không còn `axios` gọi REST.
- Lý do cần `NetClient` thông minh: nếu không, độ tin cậy sẽ nằm ở "tầng web" — đúng điều thầy không muốn. Vì thế client console C là bằng chứng song song.

---

## 7. Lộ trình học và làm (mỗi bước có bài tập và cách kiểm tra)

**Cách làm việc giữa chúng ta:** bạn tự viết code C. Mỗi bước mình giải thích khái niệm, đưa đề bài và tiêu chí đúng, bạn làm, rồi mình đọc và chỉ lỗi (gợi ý, không viết hộ).

| # | Bước | Học gì | Kiểm tra bằng |
|---:|---|---|---|
| 0 | Môi trường | WSL Ubuntu, gcc, make, gdb, `valgrind`/`-fsanitize=address`, `nc`, `tcpdump` | `gcc` chạy được, mở được Wireshark với file `.pcap` |
| 1 | Echo server | `socket/bind/listen/accept/recv/send`, `SO_REUSEADDR`, bắt `SIGPIPE`, đóng fd đúng | `nc localhost 9000` gõ gì nhận lại đó |
| 2 | **Framing** | TCP là luồng byte, đọc nửa gói, dính gói | gửi 1 gói từng byte một và 3 gói dính nhau; server đều ghép đúng |
| 3 | Header + CRC32 | endianness (`htonl`), kiểm tra `magic/length`, CRC | test đơn vị: gói đúng/sai CRC/length quá lớn |
| 4 | Nhiều client | non-blocking, `epoll`, buffer nhận/ghi mỗi kết nối, `EPOLLOUT` | 100 `nc` cùng lúc; client chậm không làm kẹt client khác |
| 5 | Heartbeat + timeout | `timerfd`, phát hiện kết nối chết | rút client không đóng → server tự dọn sau N giây |
| 6 | Client C + retry | bảng yêu cầu chờ, timeout, backoff | server cố ý bỏ phản hồi → client gửi lại và thành công |
| 7 | Dedupe cache | idempotency, TTL, giới hạn bộ nhớ | gửi 2 lần cùng `req_id`, handler chỉ chạy 1 lần |
| 8 | Lossy proxy | mô phỏng drop/delay/corrupt/dup | chạy lại các kịch bản mục 4.4 qua proxy |
| 9 | Log + Monitor | định dạng log, che dữ liệu, bộ đếm | log khớp thao tác; `MON_STATS` hiện số đúng |
| 10 | Bridge ↔ Go | framing lần hai, ánh xạ `conn_id`, đẩy push, kết nối lại | đăng nhập thật qua C → Go → Postgres |
| 11 | Session + RESUME | hàng đợi push chưa ack, vòng đệm | ngắt mạng, kết nối lại, nhận đủ thông báo |
| 12 | **WebSocket** | HTTP Upgrade, `Sec-WebSocket-Accept` (SHA-1 + Base64), frame có mask, ping/pong/close, phân mảnh | trình duyệt `new WebSocket("ws://...")` bắt tay thành công, echo chạy |
| 13 | `NetClient` JS | `DataView`, mirror quy tắc | FE đặt vé qua WS; kịch bản mục 4.4 chạy được trên trình duyệt |
| 14 | **Truyền file bậc 2** (tải vé điện tử PDF) | tái sử dụng HW7: mảnh 4096 byte, cửa sổ trượt, ACK tích lũy, RTO, fast retransmit, PROBE, hash cuối file | tải file qua proxy gây lỗi: file nhận về đúng hash |
| 15 | Nâng cao (chọn 1–2) | TLS (OpenSSL), rate limit, UDP ARQ, "server hãng giả" gọi song song | tùy mục chọn |

Thứ tự gợi ý: 1–5 là nền (khoảng 1 tuần), 6–9 là phần thầy chấm nặng nhất, 10–13 để nối vào hệ thống. Bước 12 có thể làm sớm hơn nếu muốn FE chạy sớm.

Sau mỗi bước nên chạy `valgrind` hoặc ASan để bắt rò bộ nhớ và truy cập ngoài biên.

---

## 8. Quyết định cần chốt

| # | Câu hỏi | Đề xuất |
|---:|---|---|
| 1 | Hệ điều hành chạy `netcore` | **WSL/Linux (epoll)**. Nếu bắt buộc Windows: `WSAPoll`/IOCP, khác API |
| 2 | Kênh C ↔ Go | Unix domain socket (nhanh, đơn giản); TCP localhost nếu Go chạy ngoài WSL |
| 3 | Payload | JSON (C không parse, chỉ chuyển tiếp) |
| 4 | `req_id` đặt ở đâu | Thêm 8 byte vào header (C đọc được mà không parse JSON) |
| 5 | Tự viết SHA-1/Base64 hay dùng OpenSSL | Tự viết (điểm cộng, ngắn) |
| 6 | Có làm UDP ARQ không | Chỉ sau khi xong bước 11 |
| 7 | TLS | Làm sau cùng, tùy thời gian |

---

## 9. Câu hỏi bảo vệ dự kiến

1. **TCP đã đảm bảo tin cậy, vì sao còn seq/ack/retry?**
   TCP lo ở tầng truyền (segment). Nó không biết yêu cầu nghiệp vụ đã được xử lý hay chưa khi kết nối đứt, không có ranh giới gói, không phát hiện lỗi ở tầng ứng dụng, không chống xử lý trùng. Các cơ chế của mình nằm ở những chỗ đó.
2. **Nếu mất phản hồi đặt vé thì sao, có bị đặt hai lần không?**
   Không. Client gửi lại cùng `req_id`, server tra bảng dedupe và trả lại kết quả cũ. DB còn có ràng buộc UNIQUE làm lớp cuối.
3. **Làm sao xử lý gói đến nửa chừng hoặc dính nhau?**
   Mỗi kết nối có bộ đệm nhận và máy trạng thái: đọc đủ 24 byte header, kiểm tra `magic/length`, đọc đủ `length` byte rồi kiểm CRC; dư byte của gói sau giữ lại.
4. **Client rớt mạng thì mất thông báo trễ chuyến không?**
   Không. Push có `seq`, giữ trong hàng đợi đến khi được ack, `RESUME` phát lại phần thiếu; quá hạn phiên thì thông báo vẫn nằm trong bảng `Notifications`.
5. **Sao vẫn dùng WebSocket, có phải xử lý hết ở web?**
   WebSocket chỉ là đường chở byte vì trình duyệt không mở TCP thô. Handshake, frame và toàn bộ logic tin cậy do mình tự cài trong C. Có client console C chạy TCP thô làm bằng chứng.
6. **Vì sao Go + C, không full C?**
   Phần mạng là mục tiêu môn học nên viết bằng C. Phần nghiệp vụ (Postgres, SMTP, Stripe, bcrypt/JWT) thì không phải trọng tâm môn học và đã có sẵn từ project cũ, thư viện Go an toàn bộ nhớ hơn.
7. **Server chịu được bao nhiêu client?**
   Dựa vào epoll non-blocking một luồng; có số liệu từ load generator và `MON_STATS`.
8. **Chống client xấu thế nào?**
   Giới hạn `length`, kiểm `magic`, timeout bắt tay, giới hạn số kết nối/IP và tốc độ gói, dọn kết nối treo.

---

## 10. Việc cần làm trong repo (khi bắt đầu)

Thứ tự làm đã chốt:

- [ ] 1. Viết `docs/PROTOCOL.md` chốt header, type, flags, mã lỗi, phân biệt ACK truyền tin với phản hồi nghiệp vụ (nguồn chân lý cho cả C, Go, JS).
- [ ] 2. Tạo `backend/netcore/` theo mục 5.2 (Makefile, `-Wall -Wextra -fsanitize=address` ở chế độ debug). Viết TCP + framing + CRC, chạy với client console C (bước 1–4).
- [ ] 3. Thêm heartbeat, retry, dedupe, lossy proxy, log, Monitor (bước 5–9), rồi truyền file bậc 2 (bước 14).
- [ ] 4. Go: `backend/internal/infra/bridge` (listener nội bộ), thay `infra/api`; nối `bridge/` ở C với Go (bước 10–11).
- [ ] 5. FE: thêm `net/socketClient.js`, `net/protocol.js`; đổi `services/` và hook gọi REST sang gửi message qua socket (bước 12–13).
- [ ] 6. Chốt chính sách hủy/đổi theo hãng (bảng chính sách), cập nhật [TONG-QUAN-DU-AN.md](TONG-QUAN-DU-AN.md): đổi Option D thành hướng chính, đánh dấu các lỗi DB đã sửa.
- [ ] 7. (Tùy chọn, làm sau cùng) "server hãng giả" gọi song song, TLS.
