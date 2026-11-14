# Phân công triển khai — Nhóm 7

> **Lưu trữ phương án Electron.** Phân công cập nhật theo GTK4 + C ở [kế hoạch hiện hành](KE-HOACH-GTK4-C.md).

> Dựa trên bảng thông tin nhóm đính kèm ngày 13/11/2026. Hai vị trí thành viên số 3 và 4 chưa có tên/MSSV; không tự gán người thực hiện. Kiến trúc và thứ tự kỹ thuật xem [hướng dẫn triển khai](HUONG-DAN-CHUYEN-SANG-ELECTRON-TCP-C.md).

## 1. Thông tin và phạm vi

| Thành viên | MSSV | Phần được ghi trong bảng gốc |
|---|---:|---|
| Nguyễn Tuấn Anh | 20235650 | Socket cùng Hiếu; log; chọn ghế; đặt vé/thanh toán mô phỏng; quản lý vé; thông báo; giao diện cùng Hiếu |
| Nguyễn Đức Hiếu | 20235712 | Socket cùng Tuấn Anh; đăng nhập/đăng ký; tìm kiếm/lọc/so sánh; sinh vé và email; giao diện cùng Tuấn Anh |

Đề tài: **8. Xây dựng ứng dụng đặt vé máy bay online**. Bản desktop dùng Electron/TypeScript; cả TCP client và TCP server viết bằng C; Go xử lý nghiệp vụ; PostgreSQL lưu dữ liệu. “Online” ở đây là ứng dụng client kết nối server qua mạng, không buộc giao diện phải chạy trong trình duyệt.

## 2. Quy ước làm chung trước khi chia nhánh

1. Cùng chốt message ID, cấu trúc request/response và các lỗi. Viết một bảng contract được ba phần C client, C server, Go và TypeScript cùng tuân theo. Phân biệt giao thức client–server 32 byte với bridge C–Go 24 byte.
2. Cùng chốt mô hình phiên: yêu cầu gốc là `Sessions` + `AUTH_RESUME`; code hiện tại là PASETO + `AUTH_TOKEN`. Chỉ triển khai một mô hình chính thức sau khi đã thống nhất đặc tả.
3. Chia file theo sở hữu để tránh sửa đè. Cả hai cùng xem thay đổi protocol, schema, payload và luồng thanh toán trước khi tích hợp.
4. Mỗi người bàn giao một luồng chạy qua app → client C → server C → Go → PostgreSQL, kèm cách chạy và trạng thái còn thiếu. Không lấy UI giả lập làm bằng chứng backend đã xong.

## 3. Công việc Nguyễn Tuấn Anh (20235650)

| Thứ tự | Việc cụ thể | Công nghệ/file chính | Kết quả bàn giao |
|---:|---|---|---|
| 1 | Cùng Hiếu chốt header, `req_id`, mã lỗi, phân chia tiến trình và thông điệp stdio | C, tài liệu protocol, TypeScript types | Bảng giao thức đủ để hai người làm song song |
| 2 | Chủ trì **TCP server C**: tiếp nhận nhiều client, đọc/ghi đủ frame, timeout/đóng kết nối, giới hạn tài nguyên, kết nối bridge Go | POSIX sockets, `pthread` hoặc `poll`, `backend/netcore/` | Health/auth/search qua cổng 9000 ổn định; lỗi kết nối được báo rõ |
| 3 | Chủ trì **log message và giám sát tầng mạng**: IN/OUT/PUSH, kết nối/ngắt kết nối, thời gian xử lý, che dữ liệu nhạy cảm, xoay file | C, file log, mutex/queue nếu ghi đa luồng | Log đủ trường trong bảng đề tài; không lộ password/token/CVV |
| 4 | Xây dựng **giữ/chọn ghế và chống trùng**: message, handler Go, transaction và unique index phù hợp; hết hạn giữ ghế | Go, PostgreSQL, sqlc, scheduler | Hai client tranh cùng ghế chỉ một client thành công |
| 5 | Làm **đặt vé và thanh toán mô phỏng**: liên hệ, hành khách, tính tổng, tạo đơn, trạng thái thanh toán; kết quả trả ngay | Go usecase/bridge, PostgreSQL, Electron UI | Đặt chỗ khách vãng lai và thành viên theo contract |
| 6 | Làm **xem chi tiết/hủy vé** ở mức được chốt; đổi vé/in PDF là phần mở rộng sau khi xem xét ghi chú GV | Go, DB, Electron/TypeScript | Luồng tối thiểu hoàn chỉnh, ghi rõ phần mở rộng chưa làm |
| 7 | Làm **push thông báo chuyến bay**: C server đẩy tới client, client C chuyển event ra stdio, Electron hiển thị | C, Go notification, Electron IPC | Khách online nhận thay đổi lịch/hủy/trễ mà không tải lại màn hình |
| 8 | Cùng Hiếu hoàn thiện UI các màn đặt chỗ, chọn ghế, thanh toán mô phỏng, vé và thông báo | React + TypeScript + Electron | UI gọi backend thật qua API IPC |

Tuấn Anh sở hữu phần **C server**; điều này không có nghĩa viết quy tắc đặt vé bằng C. Go thực thi nghiệp vụ để transaction và truy vấn DB nằm cùng chỗ.

## 4. Công việc Nguyễn Đức Hiếu (20235712)

| Thứ tự | Việc cụ thể | Công nghệ/file chính | Kết quả bàn giao |
|---:|---|---|---|
| 1 | Cùng Tuấn Anh chốt giao thức; chủ trì **TCP client C trên Windows**, dùng Winsock và frame 32 byte, vòng đọc/ghi, timeout/nối lại, JSON Lines qua stdio | C, Winsock, `client/native/` đề xuất | Binary client C nhận lệnh từ Electron và gửi TCP thật |
| 2 | Tạo **Electron main/preload + TypeScript**, khởi chạy client C, IPC theo API hẹp, xử lý C thoát và request đang chờ | Electron, TypeScript, Node `child_process` | App desktop gọi được health qua client C |
| 3 | Làm **đăng ký, đăng nhập, đăng xuất, nối lại phiên, phân quyền** theo mô hình đã chốt; giới hạn sai mật khẩu và chỉnh schema nếu cần | Go auth/bridge, PostgreSQL, Electron UI | Auth qua TCP; quyền admin bị kiểm tra phía Go |
| 4 | Làm **tìm kiếm/lọc/so sánh chuyến** theo thành phố hoặc IATA đã chốt, ngày, số khách, hạng ghế, giá và thời lượng; cache nếu cần | Go flight usecase/query, PostgreSQL, TS UI | Kết quả từ DB, bộ lọc hợp lệ cả client và server |
| 5 | Làm **mã đặt chỗ/mã vé và email** sau thanh toán giả lập, có cơ chế gửi lại/thử lại nếu nằm trong phạm vi | Go, PostgreSQL, email worker; PDF/QR là phần mở rộng | Mã duy nhất; email hoặc outbox mô phỏng truy vết được |
| 6 | Cùng Tuấn Anh chuyển các màn login, signup, tìm kiếm, danh sách và so sánh từ React/Vite sang API IPC | React + TypeScript + Electron | Không còn gọi REST cho các luồng đã chuyển |

Hiếu sở hữu phần **C client**; Electron main chỉ quản lý tiến trình C và chuyển dữ liệu qua stdio, không mở TCP socket bằng Node.js.

## 5. Các mục chung và mục chưa phân công

| Mục trong bảng gốc | Trạng thái phân công | Đề xuất xử lý |
|---|---|---|
| Kết nối socket (2 điểm) | Cả hai | Tuấn Anh server C, Hiếu client C; cùng chốt giao thức và tích hợp |
| Giao diện (3 điểm) | Cả hai | Hiếu khung Electron/auth/search; Tuấn Anh booking/seat/ticket/notification |
| Admin quản lý chuyến/vé/khách (mục 7) | Chưa ghi người | Để trong backlog, chốt người nhận và phạm vi sau luồng lõi |
| Admin thống kê doanh thu (mục 8) | Chưa ghi người | Chỉ làm khi Payments mô phỏng có dữ liệu thật; không dùng số cố định |
| Admin giám sát server (mục 9) | Chưa ghi người | Cần C server xuất metrics/log có phân quyền, sau đó làm UI admin/console |
| Thành viên số 3 và 4 | Trống | Chỉ cập nhật khi nhóm có thông tin thật |

## 6. Mốc làm việc và phụ thuộc

| Mốc | Tuấn Anh | Hiếu | Điều kiện chuyển mốc |
|---|---|---|---|
| 0 — hợp đồng | Header, server API, log format | Client API, stdio schema, TypeScript types | Một contract được cả hai dùng |
| 1 — thông đường | C gateway/bridge health | Winsock client + Electron health | Health đi trọn chuỗi và hiện trong UI |
| 2 — tài khoản/chuyến | Hỗ trợ server và log | Auth + search/filter/compare | Dữ liệu thật, ngắt/nối lại rõ ràng |
| 3 — đơn/ghế | Giữ ghế, booking, payment mô phỏng | UI đặt vé, mã vé/email | Không bán trùng ghế; kết quả thanh toán nhất quán |
| 4 — hoàn thiện | Vé, push, log | UI vé, xử lý phiên, bàn giao | Chạy được trên nhiều client; tài liệu cách chạy đầy đủ |

**Ghi chú từ giảng viên trong bảng nhóm:** thanh toán là **giả lập giữa client và server**; quản lý/in/đổi vé được nhận xét là phức tạp và cần cân nhắc bỏ bớt. Vì vậy ưu tiên luồng đăng nhập → tìm chuyến → giữ ghế → đặt chỗ → thanh toán giả lập → xác nhận; phần PDF/QR, đổi vé và admin làm sau khi đã chốt phạm vi và có người nhận.

## 7. Công nghệ thống nhất

| Công nghệ | Dùng để làm gì | Người phụ trách chính |
|---|---|---|
| C + POSIX sockets + `pthread`/`poll` | TCP server, framing, nhiều client, log/push | Tuấn Anh |
| C + Winsock2 | TCP client Windows, codec/protocol, stdio adapter | Hiếu |
| Electron + TypeScript + React/Vite | Giao diện desktop, main/preload/IPC | Hiếu khung; cả hai làm màn hình |
| Go | Auth, search, booking, payment giả lập, thông báo, email | Theo chức năng ở mục 3–4 |
| PostgreSQL + sqlc/migration | Tài khoản, chuyến, ghế, đơn, vé, thanh toán, thông báo | Theo chức năng ở mục 3–4 |

Không đưa mật khẩu, session, thông tin giấy tờ hoặc dữ liệu thẻ vào log. Với thanh toán mô phỏng, không yêu cầu người dùng nhập số thẻ/CVV thật.
