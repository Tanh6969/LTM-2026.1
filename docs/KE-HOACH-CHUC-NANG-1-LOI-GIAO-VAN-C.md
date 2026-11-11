# Kế hoạch triển khai chức năng 1: Lõi giao vận C

> Phạm vi kế hoạch: chỉ chức năng 1 trong giai đoạn 1 — kết nối client–server bằng socket và lõi giao vận C chạy handler echo. Chức năng 10 (log/giám sát) chưa làm trong kế hoạch này. Chưa nối Go, chưa làm WebSocket và chưa triển khai nghiệp vụ đặt vé.

## 1. Mục tiêu

Nâng cấp TCP echo demo thành một lõi giao vận C có thể nhận nhiều client, đóng/mở khung thông điệp, phát hiện CRC sai, heartbeat, gửi lại và chống xử lý trùng. Client console C và proxy gây lỗi sẽ cung cấp cách tái hiện các tình huống mạng. Có thể dùng thông báo chạy/debug ngắn gọn; log từng gói và bộ đếm thuộc chức năng 10, ngoài phạm vi hiện tại.

Ở giai đoạn này payload là dữ liệu opaque đối với giao vận; handler chỉ echo lại payload. Sáu kịch bản kiểm tra lấy theo mục 4.4 của `KIEN-TRUC-MANG-C-GO.md`: phân mảnh, mất phản hồi/gửi lại, CRC sai, heartbeat timeout, ngắt rồi nối lại, và tải nhiều client. Kịch bản nối lại chỉ kiểm tra khả năng kết nối lại TCP ở phạm vi này; khôi phục phiên và phát lại push thuộc bước sau.

## 2. Tài liệu và hiện trạng đã xem

- `docs/KIEN-TRUC-MANG-C-GO.md`: đặc tả header 24 byte, CRC32, framing, epoll, heartbeat, retry/dedupe, proxy và lộ trình bước 0–9.
- `docs/TONG-QUAN-DU-AN.md`: giai đoạn 1 gắn chức năng 1 và 10; yêu cầu hiện tại chỉ lấy phần lõi giao vận thuộc chức năng 1. Chức năng 10 được để lại riêng.
- `docs/BRIDGE-PROTOCOL.md`: giao thức bridge nội bộ đã có, nhưng không thuộc phạm vi giai đoạn echo độc lập này.
- `TCP server demo/tcp_server.c`, `tcp_client.c`, `Makefile`: ví dụ TCP blocking, echo theo từng client, gửi/nhận chuỗi chưa có framing; đây là tài liệu tham khảo để học socket, không đủ làm lõi giao vận.

## 3. Nguyên tắc và ranh giới

1. Làm tuần tự theo bước 0–9 trong kiến trúc; mỗi bước phải chạy được trước khi mở rộng bước kế tiếp.
2. Dùng Linux/WSL cho `epoll`, như lựa chọn được đề xuất trong kiến trúc. Đường dẫn/Makefile nên thuận tiện build trong WSL; không giả định `epoll` chạy native Windows.
3. Giữ handler echo độc lập với tầng framing/reliability. Không parse JSON và không gọi Go.
4. Định nghĩa wire format duy nhất dùng chung giữa server, client và proxy; không gửi struct C thô lên mạng. Encode/decode số nguyên theo big-endian.
5. Bảo vệ mọi kích thước: kiểm tra magic, version, độ dài tối đa và CRC trước khi chuyển payload cho handler.
6. Giữ thông báo chạy/debug ngắn gọn. Ghi log từng gói, thống kê và monitor thuộc chức năng 10, ngoài phạm vi hiện tại.

## 4. Kế hoạch theo bước

| Bước | Hạng mục | Nội dung dự kiến | Kết quả kiểm tra |
|---:|---|---|---|
| 0 | Môi trường | Xác nhận WSL/Linux, gcc, make; chọn cổng dev và cách chạy client/server/proxy | Biên dịch được chương trình C nhỏ; lệnh build/run được ghi trong README hoặc Makefile |
| 1 | Echo server nền | Tách server/client mới khỏi demo; socket, bind, listen, accept, gửi đủ byte, xử lý đóng kết nối và lỗi ghi | Một client gửi payload nhận lại chính xác; không phụ thuộc dấu xuống dòng/chuỗi C |
| 2 | Framing TCP | Buffer nhận theo kết nối; state machine đọc header/payload đủ; giữ byte dư; xử lý header/payload đến phân mảnh hoặc dính nhau | Một frame gửi từng byte và nhiều frame gửi liền được giải mã chính xác |
| 3 | Header + CRC32 | Encode/decode header 24 byte theo spec; magic/version/length; CRC32 trên header (bỏ trường CRC) + payload | Frame đúng được echo; CRC sai, magic sai và length quá giới hạn bị từ chối an toàn |
| 4 | Nhiều client bằng epoll | Socket non-blocking; event loop epoll; state/buffer/hàng đợi ghi riêng; xử lý partial write và client chậm | Nhiều client đồng thời không chặn lẫn nhau; kết nối được dọn đúng |
| 5 | Heartbeat | PING/PONG và thời điểm hoạt động cuối; quét timeout bằng timerfd hoặc cơ chế timer tương đương | Client ngừng gửi heartbeat bị timeout và đóng; log ghi rõ nguyên nhân |
| 6 | Client console C + retry | CLI gửi frame, theo dõi request chờ phản hồi; timeout/backoff; retry giữ nguyên req_id, đánh dấu RETRANS | Khi proxy bỏ phản hồi, client tự gửi lại và nhận kết quả echo |
| 7 | Chống trùng | Cache `(session hoặc client, req_id)` có TTL/giới hạn; lưu phản hồi echo; retry trùng trả kết quả đã lưu, không gọi handler lần nữa | Gửi cùng req_id hai lần chỉ chạy handler một lần; cache hết hạn/đầy không tăng bộ nhớ vô hạn |
| 8 | Lossy proxy | Chuyển tiếp TCP hai chiều theo frame; cấu hình drop phản hồi, delay, corrupt và duplicate; seed/tùy chọn xác định để tái hiện | Mỗi chế độ lỗi có thể bật/tắt và kết quả tác động quan sát được; sáu kịch bản có thể chạy lặp lại |
| 9 | Hoàn thiện lõi echo | Rà soát luồng frame, lỗi socket, dọn kết nối và chạy lại các tình huống; chỉ dùng thông báo tối thiểu để biết server/client đang chạy | Các hành vi giao vận cần thiết cho chức năng 1 ổn định; không yêu cầu log từng gói, màn hình monitor hay bộ đếm |

## 5. Giao thức và quyết định cần khóa trước bước 2–3

Kiến trúc hiện mô tả header 24 byte: `magic` 2, `version` 1, `flags` 1, `type` 2, `reserved` 2, `seq` 4, `ack` 4, `length` 4, `crc32` 4; CRC32 tính trên header trừ trường CRC và payload. `req_id` chưa được chốt vị trí (đầu payload hay trường header 8 byte). Trước khi viết encode/decode cần thống nhất:

- Vị trí và kích thước `req_id`; đề xuất chọn một định dạng cố định để client, server và proxy đọc giống nhau.
- CRC32 variant/đoạn khởi tạo/final XOR cụ thể, cùng test vector để tránh hai phía tính khác nhau.
- Giá trị `type`/cờ cho ECHO, PING/PONG, ACK/NACK và phản hồi; quy tắc seq/ack tối thiểu ở giai đoạn echo.
- `MAX_PAYLOAD`, timeout heartbeat, timeout retry, số lần retry, TTL và giới hạn cache dedupe.
- Drop ở proxy có nghĩa bỏ frame chiều client→server hay server→client. Để chứng minh application retry trên TCP, cần chủ yếu bỏ phản hồi server→client; TCP không biết frame bị bỏ ở proxy.

Đề xuất tạo một spec nhỏ (trong thư mục `backend/netcore/include` hoặc tài liệu protocol hiện có) trước khi cài đặt để tránh hằng số bị lặp và lệch nhau.

## 6. Cấu trúc mã dự kiến

Ưu tiên tạo lõi độc lập dưới `backend/netcore/`, theo cấu trúc trong mục 5.2 của kiến trúc:

```text
backend/netcore/
├── include/          # định nghĩa header, frame, connection và hằng số giao thức
├── src/
│   ├── main.c
│   ├── transport/    # listen, epoll, buffer nhận/hàng đợi ghi
│   ├── protocol/     # encode/decode frame, CRC32
│   ├── reliability/  # retry phía client, dedupe phía server, heartbeat
│   # Không tạo logging/ hoặc monitor trong phạm vi chức năng 1
├── client/           # client console TCP C
├── tools/            # lossy proxy
├── tests/            # test frame/CRC/dedupe và kịch bản tích hợp
└── Makefile
```

Từng thư mục có thể khởi đầu ít file và tách module khi trách nhiệm đã rõ. Không sửa demo cũ cho đến khi xác định có thể tái sử dụng gì; giữ demo làm ví dụ socket căn bản.

## 7. Kịch bản kiểm tra chức năng 1

| # | Kịch bản | Cách tạo tình huống | Bằng chứng đạt |
|---:|---|---|---|
| 1 | Frame phân mảnh | Client gửi header/payload từng phần hoặc từng byte | Server chỉ gọi echo sau khi nhận đủ frame; client nhận payload đúng |
| 2 | Mất phản hồi và retry | Proxy bỏ một phản hồi; client timeout và gửi lại cùng req_id | Client cuối cùng nhận echo; dedupe không gọi handler lần hai |
| 3 | CRC sai | Proxy đổi byte payload hoặc test client tạo CRC không khớp | Frame hỏng bị từ chối, không được echo như payload hợp lệ |
| 4 | Heartbeat timeout | Dừng PING hoặc ngắt luồng client mà không gửi đóng sạch | Server phát hiện quá hạn và đóng kết nối |
| 5 | Ngắt rồi kết nối lại | Đóng client rồi chạy lại/kết nối lại qua cùng proxy | Kết nối mới hoạt động và echo bình thường; không tuyên bố khôi phục session/push ở giai đoạn này |
| 6 | Tải nhiều client | Chạy client C/script song song (mốc mục tiêu ban đầu 100; ghi rõ nếu máy không đạt) | Các client hoàn thành echo; server không treo và client chậm không làm nghẽn client khác |

## 8. Tiêu chí hoàn thành giai đoạn

- Build server, client console và proxy bằng hướng dẫn lặp lại được trên WSL/Linux.
- Handler echo chạy qua frame có header và CRC; server phục vụ nhiều client bằng epoll.
- Heartbeat timeout, retry và dedupe có hành vi quan sát được qua kết quả client/server.
- Proxy hỗ trợ tối thiểu drop, delay, corrupt và duplicate theo frame.
- Sáu kịch bản ở mục 7 chạy được và hành vi echo, gửi lại, từ chối frame hỏng có thể quan sát trực tiếp.
- Không có phụ thuộc vào Go, DB, frontend hoặc bridge trong giai đoạn này.

## 9. Trình tự bắt đầu đề xuất

1. Chốt các quyết định giao thức ở mục 5 và lưu spec nguồn chân lý.
2. Tạo skeleton `backend/netcore/` cùng Makefile và lệnh chạy.
3. Làm bước 1–3 thành một echo framed đơn client; kiểm tra phân mảnh và CRC.
4. Thêm epoll/partial write, rồi heartbeat.
5. Xây client console có req_id/retry; sau đó thêm dedupe.
6. Thêm proxy và kịch bản demo; hướng dẫn chạy sáu kịch bản; để log từng gói, bộ đếm và monitor sang phần chức năng 10.

## 10. Ngoài phạm vi

WebSocket, bridge C–Go, đăng nhập/nghiệp vụ, session `RESUME` và push tin cậy, truyền file, TLS, UDP ARQ, giao diện monitor và thay đổi frontend thuộc các bước/giai đoạn sau. Kế hoạch hiện tại chỉ tạo nền giao vận TCP cho chức năng 1 và minh chứng bằng echo.



