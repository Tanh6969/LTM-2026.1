# Kế hoạch chuyển sang GTK4 + C

> Quyết định ngày 13/11/2026: ứng dụng desktop Windows dùng **GTK4 + C**, client TCP viết bằng C, server TCP viết bằng C, Go xử lý nghiệp vụ, PostgreSQL lưu dữ liệu. Kế hoạch này thay phương án Electron trong `HUONG-DAN-CHUYEN-SANG-ELECTRON-TCP-C.md` và `PHAN-CONG-NHOM-7.md`; hai tài liệu đó chỉ còn giá trị tham khảo về hiện trạng, yêu cầu và phân công ban đầu.

## 1. Kiến trúc đích

```text
Ứng dụng Windows (một tiến trình C)
  GTK4 UI → controller → service/client TCP C (Winsock)
    → TCP frame 32 byte + JSON → server C/POSIX :9000
    → bridge nội bộ frame 24 byte → Go :9100
    → usecase/repository → PostgreSQL
```

- Không có Electron, React, TypeScript, Node.js, IPC hay tiến trình client C riêng. `frontend/` hiện tại là **mẫu về luồng và nội dung UI**, không được gọi khi chạy app GTK4.
- GTK4 chỉ hiển thị và nhận thao tác; C client giữ kết nối, mã hóa/giải mã frame, quản lý request/response/push. Go vẫn là nơi quyết định quyền, giá, giữ ghế, đặt vé và transaction DB.
- Server C hiện dùng POSIX và chạy Linux/WSL; client Windows dùng Winsock. Khi muốn server chạy Windows native, cần port riêng phần server.
- GTK4 là toolkit desktop C chạy trên Windows qua GDK/Win32, nhưng giao diện không tự động giống bộ điều khiển WinUI. Cần thiết kế lại layout, tương tác và asset cho desktop.

## 2. Phần giữ lại và phần phải viết lại

| Phần repo | Kế hoạch |
|---|---|
| `backend/netcore/src/transport/bridge_gateway.c` | Giữ làm server C nền; hoàn thiện kết nối, log, concurrency, push và lỗi |
| `backend/netcore/include/echo_transport.h` | Là hợp đồng **client ↔ server**: header 32 byte, CRC32, `seq`, `req_id`, payload tối đa 1 MiB |
| `backend/netcore/include/bridge.h`, Go `internal/infra/bridge/` | Giữ bridge **C ↔ Go** 24 byte; bổ sung message nghiệp vụ |
| `backend/internal/domain/`, repository, sqlc, PostgreSQL | Tái sử dụng; thêm usecase/query/migration theo tính năng |
| `frontend/` React/Vite | Dùng tham chiếu bố cục và luồng; viết lại widget/controller bằng GTK4 C |
| `TCP server demo/` | Giữ làm tài liệu học socket, không dùng làm server chính |

**Ranh giới cần báo cáo đúng:** Go hiện mở TCP socket ở bridge nội bộ `:9100`. Nếu yêu cầu “mọi socket trong toàn hệ thống đều bằng C”, phải đổi kênh này sang pipe/IPC khác. Nếu yêu cầu “TCP client và TCP server phía người dùng bằng C”, kiến trúc trên đã phù hợp.

## 3. Công cụ và môi trường

### Máy Windows làm client

1. Cài MSYS2, mở **UCRT64 shell**; cài GTK4, GCC toolchain, Meson/Ninja và pkg-config của cùng môi trường UCRT64. Hướng dẫn chính thức của GTK cho Windows dùng gói `mingw-w64-ucrt-x86_64-gtk4` và toolchain tương ứng: <https://www.gtk.org/docs/installations/windows>.
2. Build client C bằng Meson (đề xuất) hoặc GCC với `pkg-config --cflags --libs gtk4`, liên kết thêm `-lws2_32`. Không trộn DLL MSYS và UCRT64. Cấu hình `SERVER_HOST` và `SERVER_PORT` cho app, mặc định phát triển `127.0.0.1:9000`.
3. Khi đóng gói `.exe`, mang theo GTK runtime, schema, theme/icon và DLL cần thiết; kiểm tra trên máy Windows sạch. Chỉ biên dịch ra `.exe` chưa đủ để phát hành app GTK4.

### Máy/server Linux hoặc WSL

1. Cài PostgreSQL, Go, GCC, make. Sao chép `backend/.env.example` thành `backend/.env` rồi đặt `DB_SOURCE`, `TOKEN_SYMMETRIC_KEY` riêng, `BRIDGE_NETWORK=tcp`, `BRIDGE_ADDRESS=127.0.0.1:9100`. Không commit `.env`.
2. Với DB **mới**, tạo database `ltm` và nạp `backend/db/full_setup.sql`; DB đã có dữ liệu phải dùng migration, không nạp lại setup.
3. Chạy Go từ `backend/` bằng `go run ./cmd/server`; chạy C server từ `backend/netcore/` bằng `make` rồi `./bin/bridge_gateway 9000 127.0.0.1 9100`.
4. Nếu server trong WSL và app GTK4 trên Windows, kiểm tra kết nối tới cổng 9000 từ Windows; khi chạy trên máy khác, cấu hình IP thật và firewall. Chỉ bridge `9100` lắng nghe loopback.

## 4. Cấu trúc client C đề xuất

```text
client/gtk4/
  meson.build
  src/main.c                 GtkApplication, khởi tạo app
  src/ui/                    GtkBuilder .ui, widget, controller từng màn
  src/app/                   state, điều hướng, định dạng dữ liệu hiển thị
  src/net/tcp_win.c          Winsock: kết nối, send/recv, đóng, timeout
  src/net/protocol.c         header 32 byte, endian, CRC, giới hạn payload
  src/net/client.c           req_id, hàng đợi request, response, push
  src/service/               hàm nghiệp vụ phía client: login, search, booking...
  resources/                 icon, ảnh, CSS GTK, GtkBuilder XML
```

Tách `protocol.c` khỏi `tcp_win.c` để dùng chung logic byte/CRC với server hoặc đối chiếu dễ dàng. Không cho controller GTK gọi `send`/`recv` trực tiếp; controller gọi service và nhận kết quả qua callback/event.

**Luồng thread:** UI chạy trên GTK main thread. Socket chạy ở worker thread để không làm treo cửa sổ khi server chậm hoặc mất mạng. Worker đưa kết quả về GTK main context bằng `g_main_context_invoke()` (hoặc cơ chế idle phù hợp), rồi mới cập nhật widget. Tất cả thao tác sửa widget nằm trên main thread. Khi đóng app, hủy request chờ, đóng socket, chờ worker dừng và giải phóng tài nguyên. [GTK threading](https://docs.gtk.org/gtk4/section-threading.html).

## 5. Giao thức và tích hợp Go

1. **Đóng băng contract** từ code hiện tại: client C ↔ server C là header 32 byte trong `echo_transport.h`; C ↔ Go là header 24 byte trong `bridge.h`. Ghi bảng message ID, JSON request/response, status, quyền truy cập và `req_id` trong một tài liệu duy nhất. Không dựa vào sơ đồ cũ ghi header client 24 byte.
2. **Xử lý TCP đúng:** `send`/`recv` có thể trả một phần. Đọc đủ header, kiểm tra magic/version/length, đọc đủ payload rồi kiểm CRC. Không giả định một lần `recv` bằng một message. Thử lại request tạo đơn/thanh toán chỉ sau khi có chống trùng theo `req_id` ở server/DB.
3. **Go hiện có:** health, đăng ký/đăng nhập/đăng xuất/nối lại token, tìm/lấy/danh sách chuyến. Go chưa nối đủ các luồng ghế, booking, thanh toán, vé, thông báo, admin vào bridge; thêm `types.go` → handler → usecase → repository/query theo từng luồng.
4. **Phiên đăng nhập:** bảng nhóm yêu cầu session DB + `AUTH_RESUME`; Go hiện dùng PASETO + `AUTH_TOKEN`. Chốt một mô hình trước khi làm màn login. Nếu giữ yêu cầu bảng nhóm, cần migration Sessions và thay auth handler/session logic; domain chuyến bay không phải viết lại.
5. **Thông báo:** C gateway hiện chủ yếu gửi response tuần tự theo request; push bất đồng bộ đến client và lên UI GTK cần thiết kế/triển khai thêm. Worker TCP phải nhận được cả response theo `req_id` và push không có request đang chờ.

## 6. Thứ tự thực hiện và tiêu chí xong

| Giai đoạn | Công việc chính | Tiêu chí bàn giao |
|---|---|---|
| 0 — Chốt thiết kế | Contract 32/24 byte, message, session, phạm vi MVP, wireframe GTK | Hai người cùng dùng một đặc tả; không còn payload mơ hồ |
| 1 — App rỗng và mạng | `GtkApplication`, cửa sổ chính, Winsock worker, health | Mở app Windows, bấm kiểm tra kết nối, nhận health qua C → C → Go |
| 2 — Tài khoản và chuyến | GTK đăng ký/login, form tìm kiếm/danh sách/chi tiết; Go handler hiện có và chỉnh payload | Dữ liệu thật từ PostgreSQL; UI không đứng khi mất mạng |
| 3 — Đặt chỗ lõi | Chọn hạng/ghế, thông tin liên hệ/hành khách, giữ ghế, booking, payment giả lập | Hai client tranh một ghế chỉ một thành công; đơn có trạng thái đúng |
| 4 — Vé và thông báo | Tra cứu/vé của tôi, hủy phạm vi tối thiểu, push thay đổi chuyến | Có thể nhận thông báo khi online; vé hiển thị dữ liệu thật |
| 5 — Hoàn thiện | Log, giới hạn tải, reconnect, quyền admin nếu còn phạm vi, đóng gói | Chạy nhiều client, lỗi rõ, `.exe` chạy trên máy sạch cùng GTK runtime |

**Phạm vi MVP nên là:** đăng ký/đăng nhập → tìm chuyến → giữ ghế → đặt chỗ → thanh toán giả lập → xác nhận/vé cơ bản. Ghi chú của GV trong bảng nhóm nói thanh toán giả lập và yêu cầu cân nhắc độ phức tạp của đổi/in vé; PDF/QR, đổi vé, admin dashboard nên xếp sau MVP. Không thu thập số thẻ thật/CVV cho thanh toán mô phỏng.

## 7. Phân công hai thành viên hiện có

| Người | Trách nhiệm chính theo bảng nhóm và kiến trúc GTK4 |
|---|---|
| Nguyễn Tuấn Anh (20235650) | Server C, framing/concurrency/log, xử lý ghế/booking/payment giả lập, vé/thông báo bên Go; GTK4 màn chọn ghế/đặt vé/vé |
| Nguyễn Đức Hiếu (20235712) | Client C Winsock + khung GTK4, auth/search/compare và mã vé/email bên Go; GTK4 màn đăng ký/login/tìm kiếm |
| Cả hai | Contract, cấu hình môi trường, tích hợp end-to-end, UI thống nhất, chia sẻ sửa đổi schema/protocol |

Các mục admin 7–9 trong bảng nhóm chưa ghi người thực hiện; chưa tự gán. Các mục này cần chốt phạm vi/người phụ trách sau khi luồng lõi chạy được.

## 8. Rủi ro cần xử lý từ đầu

- **Viết lại UI:** React component không chuyển trực tiếp thành GTK widget. Có thể tái sử dụng ảnh, văn bản, luồng màn và ý tưởng bố cục, nhưng mã giao diện phải viết lại.
- **Dễ treo UI:** tuyệt đối không chờ `recv()` trên main thread GTK.
- **Giao thức lệch:** mã hóa một hợp đồng chung, giữ message ID/endianness/CRC khớp hai phía; cập nhật tài liệu khi protocol đổi.
- **Nhiều client:** gateway hiện dùng một `pthread` mỗi client, cần giới hạn số kết nối và dọn thread/socket; xem xét `poll`/pool khi tải tăng.
- **Mạng công khai:** TCP hiện chưa mã hóa. Chỉ dùng môi trường tin cậy khi demo; muốn đưa lên Internet phải có TLS hoặc kênh bảo vệ tương đương cho mật khẩu và session.

## 9. Việc đầu tiên nên làm

Tạo `client/gtk4/` với một cửa sổ có nút **Kiểm tra kết nối** và vùng trạng thái. Nút gọi service C; worker Winsock gửi `SYS_HEALTH` qua frame 32 byte tới C gateway; callback cập nhật GTK. Khi luồng này chạy ổn trên Windows, mới chuyển từng màn React sang GTK4. Cách bắt đầu GTK bằng `GtkApplication` và `GtkBuilder` được mô tả trong [tài liệu GTK4](https://docs.gtk.org/gtk4/getting_started.html).
