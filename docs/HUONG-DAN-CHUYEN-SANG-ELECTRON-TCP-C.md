# Hướng dẫn chuyển dự án sang Electron + TCP C + Go

> **Lưu trữ phương án cũ.** Nhóm đã chuyển sang GTK4 + C; xem [kế hoạch hiện hành](KE-HOACH-GTK4-C.md).

> Kiến trúc đã chốt ngày 13/11/2026. Đây là kế hoạch triển khai, không phải mô tả các tính năng đã hoàn thành. Xem [phân công nhóm 7](PHAN-CONG-NHOM-7.md).

## 1. Kiến trúc và ranh giới

```text
React + TypeScript trong Electron renderer
  → API hẹp qua preload/contextBridge → Electron main (IPC)
  → stdin/stdout, JSON Lines → chương trình TCP client C (Winsock trên Windows)
  → TCP, giao thức khung 32 byte → C gateway/server (:9000, hiện chạy Linux/WSL)
  → bridge nội bộ 24 byte → Go (:9100) → usecase/repository → PostgreSQL
```

- Renderer chỉ lo giao diện. Electron main khởi chạy và giám sát tiến trình C bằng `child_process.spawn`; không dùng `net`, WebSocket hay HTTP để gọi nghiệp vụ.
- Client C giữ kết nối TCP, mã hóa/giải mã frame, ghép `req_id`, timeout và nối lại. `stdout` chỉ chứa phản hồi máy đọc được; log chẩn đoán đi `stderr`.
- Server C sở hữu cổng công khai, vòng đời kết nối và giao thức trên mạng. Go giữ xác thực, quy tắc vé, transaction và truy vấn dữ liệu.
- Bridge C–Go hiện dùng TCP nội bộ. Nếu tiêu chí của giảng viên là **mọi socket của toàn hệ thống đều phải ở C**, cần thay riêng bridge này bằng IPC dạng pipe. Với tiêu chí client–server TCP bằng C, có thể giữ bridge hiện tại.
- Electron vẫn dùng Chromium để vẽ giao diện. Điều này không làm đường giao tiếp nghiệp vụ thành HTTP/WebSocket.

## 2. Hiện trạng cần biết trước khi sửa

| Phần | Trong repo hiện tại | Việc tiếp theo |
|---|---|---|
| Frontend | `frontend/` là React/Vite JavaScript, nhiều nơi gọi REST và dùng dữ liệu giả | Thêm Electron/TypeScript, chuyển từng màn hình sang API IPC chung |
| C server | `backend/netcore/src/transport/bridge_gateway.c` nhận TCP ở `:9000`, mỗi client một `pthread` | Hoàn thiện giới hạn kết nối, đóng phiên, log, push, xử lý lỗi |
| Protocol công khai | `backend/netcore/include/echo_transport.h`: header 32 byte, CRC32, `seq`, `req_id`, payload tối đa 1 MiB | Client C phải dùng **đúng** định dạng này; không gửi JSON trần |
| Bridge nội bộ | `backend/netcore/include/bridge.h` và `backend/internal/infra/bridge/`: header 24 byte | Giữ riêng với protocol công khai; không cho client nối thẳng `:9100` |
| Go | Đã có `SYS_HEALTH`, `AUTH_REGISTER/LOGIN/LOGOUT/TOKEN`, `FLIGHT_SEARCH/GET/LIST` | Bổ sung message, handler, usecase còn thiếu |
| CSDL | PostgreSQL, schema và dữ liệu mẫu ở `backend/db/full_setup.sql`; có migration/sqlc | Chọn **một** đường tạo CSDL cho môi trường mới, cập nhật schema rồi sinh lại sqlc nếu đổi query |

`TCP server demo/` chỉ là echo POSIX dùng để học API socket; không dùng làm server đặt vé. `backend/netcore/README-CHUC-NANG-1.md` ghi rõ gateway hiện mới ở giai đoạn đầu; tài liệu kiến trúc cũ có mô tả header 24 byte cho client, nhưng **code hiện tại dùng 32 byte ở cổng 9000**.

## 3. Chuẩn bị môi trường

**Máy phát triển Windows:** cài Git, Node.js/npm, Go, PostgreSQL và Visual Studio Build Tools hoặc MinGW-w64 để biên dịch client C với Winsock (`Ws2_32`). Cài WSL2 với một bản Linux, `gcc` và `make` để build server C POSIX hiện có. Các phiên bản cụ thể nên khóa trong dự án khi bắt đầu thêm Electron; tránh phụ thuộc bản mới nhất không kiểm soát.

**Cấu hình Go:** sao chép `backend/.env.example` thành `backend/.env`, đặt `DB_SOURCE`, khóa token riêng cho môi trường, `BRIDGE_NETWORK=tcp` và `BRIDGE_ADDRESS=127.0.0.1:9100`. Không commit `.env`. Chỉ cho bridge lắng nghe loopback; cổng client C là `9000`. Nếu Go chạy trên Windows và C server chạy WSL, phải kiểm tra đường loopback giữa hai môi trường; phương án đơn giản hơn là chạy **Go và C server cùng trong WSL**, PostgreSQL cùng WSL hoặc tại địa chỉ mà WSL truy cập được.

**Khởi tạo DB:** tạo database `ltm`; với database trống có thể nạp `backend/db/full_setup.sql` theo hướng dẫn hiện có. Không nạp lại file setup lên database đã có dữ liệu vì file này có thao tác tạo lại schema. Khi có thay đổi tiếp theo, viết migration thay vì sửa dữ liệu đang chạy bằng tay.

**Thứ tự chạy hiện tại (trong WSL):**

```sh
# Sau khi PostgreSQL sẵn sàng và backend/.env đã cấu hình:
cd backend
go run ./cmd/server
```

```sh
# Terminal thứ hai:
cd backend/netcore
make
./bin/bridge_gateway 9000 127.0.0.1 9100
```

Chạy `./bin/echo_client 127.0.0.1 9000 1` rồi gửi `{}` để kiểm tra đường C → Go hiện tại. Đây là công cụ CLI thử nghiệm trên Linux, **chưa phải client Windows/Electron**.

## 4. Chốt hợp đồng giao tiếp trước khi chia việc

### 4.1. Electron ↔ client C

Đề xuất **JSON Lines UTF-8**, mỗi lệnh/đáp ứng chiếm đúng một dòng. Đóng băng schema trong `docs/` và định nghĩa type tương ứng ở TypeScript/C trước khi làm UI:

```json
{"id":"ui-42","op":"request","type":"FLIGHT_SEARCH","payload":{"departureCity":"Hà Nội","arrivalCity":"TP Hồ Chí Minh","flightDate":"2026-11-20"}}
{"id":"ui-42","ok":true,"status":0,"payload":{"data":[]}}
{"event":"connection","state":"disconnected"}
```

Đây là **đề xuất định dạng mới**, chưa có trong code. Electron main giữ bảng `id → Promise`, timeout, từ chối tất cả request đang chờ khi C thoát, và khởi chạy lại C theo chính sách nối lại. Chỉ main process ghi vào stdin; renderer không được gửi câu lệnh tùy ý. Không đưa password, token, số thẻ vào log. Chương trình C nhận `host/port` từ cấu hình hoặc tham số khởi chạy, không nhận lệnh shell từ UI.

### 4.2. Client C ↔ server C

Tái sử dụng hợp đồng ở `include/echo_transport.h` (32 byte, big-endian, CRC32). Bên client Windows cần một implementation tương đương dùng Winsock; có thể tách phần codec byte/CRC không phụ thuộc OS để dùng chung, còn phần socket để riêng POSIX/Winsock. Đọc đủ header rồi đúng `length` byte; `send` và `recv` có thể xử lý từng phần. Giới hạn 1 MiB, kiểm tra magic/version/CRC trước khi parse payload. Giữ `req_id` ổn định khi thử gửi lại và thiết kế chống xử lý trùng trước khi cho phép retry các lệnh tạo đơn/thanh toán.

Message ID hiện có: `0x0001` health; `0x0101–0x0104` auth; `0x0201–0x0203` chuyến bay. Ghi mọi message ID mới ở **một bảng versioned** và đồng bộ enum C/Go/TypeScript. Payload nghiệp vụ là JSON UTF-8; `type/status` nằm trong header. C gateway hiện trả response theo tuần tự trên mỗi kết nối; **chưa có luồng push bất đồng bộ hoàn chỉnh**, nên thông báo realtime là giai đoạn nâng cấp, không được giả định đã chạy.

## 5. Thứ tự triển khai

1. **Đóng băng giao thức và phạm vi MVP:** xác nhận message, trường JSON, status, quyền truy cập, mẫu response; ưu tiên health → đăng ký/đăng nhập → tìm kiếm → giữ ghế/đặt vé → thanh toán mô phỏng → xem vé/thông báo. Chốt cách xử lý khách vãng lai và session trước khi sửa schema.
2. **Viết client C Windows:** tạo thư mục đề xuất `client/native/`; Winsock `WSAStartup`/`socket`/`connect`/`send`/`recv`/`closesocket`; codec 32 byte; vòng đọc ghi; JSON Lines trên stdio; đóng sạch và báo lỗi. Giữ `backend/netcore/` cho server. Dùng mẫu health và login để thông đường.
3. **Thêm Electron + TypeScript:** giữ React/Vite hiện có, bổ sung cấu hình TypeScript và mã `electron/main`, `electron/preload`; đóng gói binary C cạnh app. Bật `contextIsolation`, tắt `nodeIntegration` trong renderer, chỉ expose API có tên như `login`, `searchFlights`, `createBooking`.
4. **Sửa frontend theo từng luồng:** gom mọi request vào một service TypeScript gọi `window.appApi`; thay `fetch`/Axios trong hooks và trang. Bỏ đường fallback trả dữ liệu giả ở luồng đã chuyển. `localStorage` hiện giữ token; nếu dùng lại PASETO, cân nhắc nơi lưu phù hợp cho desktop và không ghi token vào log. Vẫn có thể để trang tĩnh không cần API.
5. **Bổ sung Go:** đăng ký message trong `internal/infra/bridge/types.go`, handler ở `handlers/`, gọi usecase/repository hiện có. Go là nơi kiểm tra dữ liệu, quyền, giá, giữ ghế và transaction PostgreSQL. Việc đổi từ React web sang Electron **không đòi viết lại domain Go**.
6. **Bổ sung DB có thứ tự:** đối chiếu schema thật với yêu cầu Sessions, khách vãng lai, PNR, ghế Held/Active, Payments mô phỏng và Notifications. Chỉ thêm bảng/cột/index cần cho message đã chốt; dùng migration và cập nhật query/sqlc.
7. **Hoàn thiện C server:** giới hạn số client/thread hoặc chuyển sang pool/I/O multiplexing khi cần; xử lý partial I/O, timeout, ngắt kết nối, reconnect, log đã che dữ liệu nhạy cảm, push và backpressure. Với các lệnh tạo đơn/thanh toán, chống xử lý trùng theo `req_id` ở lớp nghiệp vụ/DB.
8. **Đóng gói:** build client C Windows, build Electron cho Windows, cấu hình địa chỉ server, phát hành server C/Go/PostgreSQL riêng. Không đóng gói PostgreSQL hay server trong app khách hàng nếu mô hình là client–server online.

## 6. Những quyết định còn phải chốt trước khi code chức năng

- **Phiên đăng nhập:** bảng phân công yêu cầu `session_id` lưu trong bảng `Sessions` và `AUTH_RESUME`; code Go hiện dùng PASETO + `AUTH_TOKEN` và logout chỉ bỏ danh tính trên kết nối. Không dùng đồng thời hai mô hình một cách mơ hồ. Nếu theo bảng đề tài, đổi Go sang session DB, thêm hạn dùng/thu hồi và migration; nếu được phép giản lược, sửa đặc tả nhóm theo PASETO.
- **Tìm kiếm:** handler hiện nhận tên thành phố và ngày; bảng đề tài muốn mã IATA, hạng ghế và số khách. Cần chốt schema request mới, xử lý server và dữ liệu sân bay trước khi nối UI.
- **Thanh toán:** ghi chú của GV là *thanh toán giả lập giữa client và server*. Dùng phương thức/giá trị mô phỏng và trạng thái giao dịch; không thu thập/lưu số thẻ thật hoặc CVV. Thành phần Stripe cũ trong frontend phải thay cho luồng này.
- **Vé và PDF:** ghi chú của GV yêu cầu cân nhắc phạm vi in/đổi/hủy vé. Bắt đầu bằng xác nhận đặt chỗ và mã vé mô phỏng; chỉ mở rộng PDF/QR, email, đổi vé khi luồng lõi ổn định.
- **Bảo mật đường truyền:** TCP tự viết hiện chưa mã hóa. Nếu chạy qua Internet, cần TLS hoặc kênh bảo vệ tương đương; không gửi mật khẩu/session trên mạng công khai bằng TCP trần.
- **Môi trường chạy server:** code C hiện là POSIX (`pthread`, `poll`, `MSG_NOSIGNAL`). Nếu muốn server Windows native, cần port sang Winsock/thread Windows; còn chạy server Linux/WSL thì không cần port phần server.

## 7. Mốc nghiệm thu theo luồng

| Mốc | Kết quả nhìn thấy |
|---|---|
| A | Electron gọi client C bằng stdio; health đi qua C server, Go và quay về UI |
| B | Đăng ký/đăng nhập, phiên được giữ theo mô hình đã chốt; tìm chuyến từ PostgreSQL |
| C | Hai client cùng chọn một ghế: chỉ một giao dịch thành công; đặt vé và thanh toán giả lập trả kết quả |
| D | Xem vé và nhận thông báo khi online; admin/monitor chỉ triển khai theo phạm vi đã nhận |

Không đánh dấu mốc hoàn thành chỉ vì màn hình hiển thị dữ liệu mẫu. Mỗi mốc phải có đường đi thật qua client C → server C → Go → PostgreSQL.
