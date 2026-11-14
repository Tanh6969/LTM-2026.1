# LTM Flight desktop (GTK4 + C)

Ứng dụng desktop C gọi C gateway ở cổng `9000` bằng protocol công khai 32 byte. Giao diện hiện nối các message Go đã đăng ký: health, đăng ký, đăng nhập, tìm chuyến, danh sách và chi tiết chuyến. Trang vé/đặt chỗ đang ghi rõ việc cần bổ sung handler Go; không dùng dữ liệu giả để báo thành công.

## Build trên Windows

1. Cài MSYS2 và mở **UCRT64** (không dùng shell MSYS mặc định).
2. Cài các gói trong UCRT64:

   ```sh
   pacman -Syu
   pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-json-glib mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf
   ```

3. Vào thư mục `client/gtk4` và build:

   ```sh
   meson setup build
   meson compile -C build
   ./build/ltm-flight.exe
   ```

Máy chạy app cần GTK runtime/DLL tương ứng; file `.exe` đơn lẻ chưa đủ để phát hành.
Ảnh nền, logo và CSS được nhúng vào `.exe` bằng GResource lúc build. Nếu Windows báo không thể ghi `ltm-flight.exe` khi biên dịch lại, hãy đóng cửa sổ ứng dụng đang chạy rồi chạy lại `meson compile -C build`.

Trang chủ dùng ảnh từ `frontend/public`: hero tự chuyển ảnh bằng hiệu ứng fade, thẻ điểm đến có ảnh và có thể bấm để điền điểm đi/đến vào form, khối giới thiệu và dải logo hãng bay. Ảnh hero được giải mã ở kích thước hiển thị để chuyển cảnh nhẹ hơn. Chuyển trang cũng dùng fade. Khi GTK tắt hiệu ứng chuyển động (`gtk-enable-animations`), app giữ ảnh tĩnh và tắt chuyển trang động. Form tìm chuyến có bố cục tab/ô tìm kiếm gần bản web; hai tab quản lý đặt vé và làm thủ tục hiển thị nhưng bị vô hiệu hóa cho tới khi có handler tương ứng ở server.

## Cấu hình

Mặc định kết nối `127.0.0.1:9000`. Có thể đặt biến môi trường `LTM_SERVER_HOST` và `LTM_SERVER_PORT` trước khi chạy app. Chạy Go backend và C gateway như trong [kế hoạch GTK4](../../docs/KE-HOACH-GTK4-C.md). Nếu app Windows và server WSL ở khác không gian mạng, đặt host thành địa chỉ mà Windows truy cập được.

## Cấu trúc mã

- `src/main.c`: GTK app, màn hình, request queue, worker thread, JSON payload/response.
- `src/net_client.c`: Winsock (hoặc POSIX), frame 32 byte, big-endian, CRC32, đọc/ghi đầy đủ.
- `src/net_client.h`: API mạng C tách khỏi GTK.
- `resources/`: logo, ảnh nền và CSS theo bộ màu của giao diện web; Meson nhúng các tệp này vào ứng dụng.

Worker là nơi duy nhất dùng socket. Kết quả được đưa về GTK main context trước khi sửa widget. Hiện client giữ một kết nối TCP cho nhiều request, xử lý tuần tự; nếu đứt kết nối, request sau sẽ tạo kết nối mới. Phiên đăng nhập gắn với kết nối cũ nên cần đăng nhập lại sau khi mất mạng. Chưa triển khai push bất đồng bộ, giữ ghế, booking hay payment; các phần này phải đồng bộ với server C/Go trước khi mở nút thao tác.
