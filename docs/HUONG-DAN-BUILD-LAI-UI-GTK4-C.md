# Hướng dẫn dựng lại UI bằng GTK4 + C

> Áp dụng cho phương án [GTK4 + C của nhóm 7](KE-HOACH-GTK4-C.md), ngày 13/11/2026. Đây là hướng dẫn **viết lại giao diện**, không phải lệnh chuyển đổi tự động React → GTK4. App GTK4 là client Windows; TCP client/server đều viết C, Go giữ nghiệp vụ, PostgreSQL giữ dữ liệu.

## 1. Mục tiêu và nguyên tắc

- Dùng `frontend/` hiện tại làm **tham chiếu nội dung, ảnh, màu, hành trình thao tác**. JSX, Tailwind, Radix, Next shims và React hooks không chạy trong bản GTK4.
- Mỗi màn có ba phần: `*.ui` (cấu trúc widget), `*_controller.c` (sự kiện và state hiển thị), `service/*.c` (gọi TCP client C). Controller không gọi `send()`/`recv()` trực tiếp.
- Dựng UI với dữ liệu mẫu **chỉ để kiểm bố cục**; một màn chỉ được đánh dấu hoàn thành khi nhận dữ liệu thật qua `client C → server C → Go → PostgreSQL`. Không dùng dữ liệu giả ngầm khi backend lỗi.
- GTK4 chạy UI trên main thread; socket và parse phản hồi chạy ngoài main thread. Đưa kết quả về main context trước khi thay widget. [GTK threading](https://docs.gtk.org/gtk4/section-threading.html).

## 2. Kiểm kê màn hiện có và bản GTK4 tương ứng

`frontend/src/App.jsx` đang định tuyến các trang dưới đây; `frontend/src/layouts/MainLayout.jsx` gắn navbar/footer. Chuyển thành một cửa sổ chính với header, vùng nội dung và trạng thái kết nối. `GtkStack` thay việc chuyển trang theo URL. [GtkStack](https://docs.gtk.org/gtk4/class.Stack.html).

| Trang/component web | Màn GTK4 cần dựng | Widget gợi ý | Nguồn dữ liệu/trạng thái |
|---|---|---|---|
| `pages/index.jsx`, `hero.jsx`, `FlightsSearch/*` | Trang chủ + form tìm chuyến | `GtkBox`, `GtkGrid`, `GtkEntry`, `GtkDropDown`, `GtkCalendar` hoặc date picker phù hợp, `GtkButton` | Form state; chuyến gợi ý từ `FLIGHT_LIST` |
| `pages/login.jsx`, `pages/signup.jsx` | Đăng nhập/đăng ký | `GtkEntry`, thuộc tính ẩn mật khẩu, `GtkButton`, `GtkLabel` báo lỗi | `AUTH_LOGIN`, `AUTH_REGISTER`; phiên theo mô hình nhóm chốt |
| `pages/flights/index.jsx`, `FlightCard`, `FlightSideFilter` | Danh sách, lọc, so sánh chuyến | `GtkListView` hoặc `GtkListBox` lúc đầu, `GtkCheckButton`, `GtkDropDown`, `GtkScrolledWindow` | `FLIGHT_SEARCH`; so sánh sau khi có message/handler tương ứng |
| `pages/confirm/index.jsx`, `PassengerInfoDialog.jsx` | Chi tiết chuyến + thông tin liên hệ/hành khách | `GtkStack`, `GtkGrid`, `GtkEntry`, `GtkSpinButton`, `GtkButton` | `FLIGHT_GET`, state form; tạo booking khi backend sẵn sàng |
| `pages/check-in/index.jsx`, `components/checkin/*` | Wizard: chuyến → hành khách → ghế → thanh toán → xác nhận | `GtkStack`, nhãn bước, `GtkGrid` ghế, `GtkButton`, `GtkSpinner` | Seat map/hold, booking, payment **giả lập**; các message chưa nối Go phải bổ sung |
| `pages/booking-management/index.jsx` | Tra cứu/chi tiết vé | `GtkEntry`, `GtkListView`, dialog xác nhận hủy | PNR và thông tin xác thực, ticket/booking handlers cần bổ sung |
| `pages/my-account.jsx` | Hồ sơ, lịch sử, đổi mật khẩu | `GtkStackSwitcher`, `GtkEntry`, `GtkListView` | Profile/history/change password handlers cần bổ sung |
| `pages/contact.jsx` | Liên hệ | `GtkBox`, `GtkLabel`, ảnh | Nội dung tĩnh hoặc nguồn được nhóm chốt |
| `pages/flights/loading.jsx`, toaster, skeleton | Trạng thái tải/lỗi/thành công | `GtkSpinner`, `GtkLabel`, `GtkInfoBar`/dialog | State request; không phải trang độc lập |
| `pages/404.jsx` | Không cần trang 404 kiểu web | Điều hướng về Home hoặc trang lỗi trong `GtkStack` | Lỗi route nội bộ |

Trang `pages/confirm/index.jsx` có tồn tại nhưng chưa được đăng ký trực tiếp trong `App.jsx`; cần quyết định đưa vào wizard hay màn chi tiết riêng. Các `components/admin/*` hiện có nhưng chưa thành các trang admin hoàn chỉnh; dựng sau luồng khách hàng nếu phạm vi còn yêu cầu.

## 3. Chuẩn bị dự án UI

Đặt client mới ở `client/gtk4/`, không sửa trực tiếp cây JSX. Trong **MSYS2 UCRT64** trên Windows, cài GTK4 và GCC theo [hướng dẫn GTK cho Windows](https://www.gtk.org/docs/installations/windows); dùng Meson/Ninja và `pkg-config` cùng môi trường UCRT64. Build liên kết GTK4 và `Ws2_32` cho Winsock. Tránh trộn thư viện UCRT64 với MSYS/MinGW khác.

```text
client/gtk4/
  meson.build
  src/main.c
  src/app_state.h, src/app_state.c
  src/navigation.h, src/navigation.c
  src/ui/main_window.ui
  src/ui/home.ui, login.ui, signup.ui, flights.ui, booking.ui, tickets.ui
  src/ui/*_controller.c
  src/service/flight_service.c, auth_service.c, booking_service.c
  src/net/protocol.c, tcp_win.c, client.c
  resources/style.css, images/, app.gresource.xml
```

Khởi đầu bằng `GtkApplication` và `GtkApplicationWindow`. Dùng `GtkBuilder` cho layout nhiều màn; đóng gói `.ui`, CSS và ảnh bằng GResource khi app ổn định để chạy độc lập với thư mục làm việc. [GTK getting started](https://docs.gtk.org/gtk4/getting_started.html).

**Khung UI:** `GtkHeaderBar` hoặc header `GtkBox` (logo, Home, Tìm chuyến, Vé của tôi, Tài khoản, trạng thái kết nối); dưới là `GtkStack`; cuối là thanh trạng thái ngắn. Nếu màn nhỏ, dùng `GtkScrolledWindow` và layout co giãn. Màu/cỡ chữ/spacing đặt trong một CSS GTK ở `resources/style.css`, không chép nguyên Tailwind CSS vì selector và thuộc tính GTK khác CSS web. [GtkCssProvider](https://docs.gtk.org/gtk4/class.CssProvider.html).

## 4. State, điều hướng và thành phần dùng chung

### State ứng dụng

Định nghĩa `AppState` quản lý trạng thái chung: kết nối (`disconnected/connecting/connected`), danh tính, phiên, chuyến đã chọn, danh sách kết quả, booking draft, thông báo. Mỗi màn giữ thêm state cục bộ như giá trị form, lỗi trường và trạng thái đang tải. Không để widget là nguồn dữ liệu duy nhất; khi chuyển màn/quay lại phải còn dữ liệu trong state.

```text
AppState
  connection_state
  session / current_user
  search_draft + search_results
  selected_flight
  booking_draft + selected_seats
  notifications
```

### Điều hướng

- Đặt tên page cố định: `home`, `login`, `signup`, `flights`, `flight-detail`, `booking`, `tickets`, `account`.
- `navigation_show(page, context)` đổi child của `GtkStack`, kiểm tra quyền trước khi vào trang cần đăng nhập và giữ `back_target` cho nút Quay lại.
- Quy trình 5 bước dùng **một `GtkStack` con** trong booking; chỉ chuyển bước khi trường bắt buộc hợp lệ. Không suy ra nghiệp vụ từ vị trí trang: server Go vẫn kiểm tra lại mọi dữ liệu.

### Component dùng lại

Tạo helper/widget cho `FlightRow`, `FormField`, `ErrorBanner`, `LoadingOverlay`, `EmptyState`, `ConnectionBadge`, `StepHeader`, `SeatButton`. Bắt đầu bằng `GtkListBox` nếu số kết quả ít; dùng `GtkListView` với model/factory khi dữ liệu lớn hoặc cần cập nhật nhiều. Đừng dựng mỗi dòng bằng một cửa sổ riêng.

## 5. Cách nối UI với client TCP C

```text
Nút GTK → controller đọc/kiểm tra input → service tạo request
→ hàng đợi client TCP C → worker Winsock gửi frame 32 byte
→ response/push → parse thành model C → callback trên GTK main context
→ cập nhật AppState và widget
```

API service nên có dạng bất đồng bộ, ví dụ `flight_service_search_async(input, callback, user_data)`; callback nhận dữ liệu đã parse hoặc lỗi gồm `status/message/field`. Controller không biết header/CRC hay cổng mạng. Client TCP phải ghép response bằng `req_id` và xử lý push riêng; khi mất kết nối, màn hiện `ConnectionBadge` và request đang chờ hoàn tất bằng lỗi rõ ràng. Không để `recv()` chạy trong callback nút bấm vì sẽ làm đóng băng giao diện.

Các message Go bridge hiện đăng ký là `SYS_HEALTH`, `AUTH_REGISTER`, `AUTH_LOGIN`, `AUTH_LOGOUT`, `AUTH_TOKEN`, `FLIGHT_SEARCH`, `FLIGHT_GET`, `FLIGHT_LIST`. Màn booking/ticket/profile chỉ nối dữ liệu thật **sau khi** handler tương ứng được bổ sung. Với auth, chốt `AUTH_TOKEN` hiện có hay `AUTH_RESUME` theo bảng nhóm trước khi viết logic giữ phiên.

## 6. Hướng dẫn dựng từng luồng

### 6.1. Khung app và kết nối

1. Tạo `main_window.ui` với header, stack, `ConnectionBadge` và trang home trống.
2. Tạo nút **Kiểm tra kết nối**. Gửi `SYS_HEALTH`; hiển thị connecting → connected/error. Gọi worker ở tầng net/service và trả về main thread.
3. Thêm trang lỗi có nút Thử lại. Xử lý đóng cửa sổ: hủy request, đóng socket, chờ worker kết thúc, rồi thoát.

### 6.2. Đăng ký/đăng nhập

1. Dựng hai form từ `login.jsx` và `signup.jsx`. Ẩn ký tự mật khẩu, báo lỗi theo trường, khóa nút khi đang gửi.
2. Gọi `AUTH_REGISTER`, `AUTH_LOGIN`. Chỉ chuyển về Home sau response thành công; lỗi 401/409 hiện rõ mà không xóa dữ liệu form không nhạy cảm.
3. Không ghi mật khẩu/token ra log hoặc hiển thị trong thông báo. Sau khi chốt mô hình phiên, xử lý khôi phục khi app mở lại và đăng xuất.

### 6.3. Tìm chuyến và chi tiết

1. Dựng form điểm đi/đến, ngày, số khách, hạng ghế. Hiện handler Go `FLIGHT_SEARCH` mới nhận `departureCity`, `arrivalCity`, `flightDate`; UI thêm các trường còn lại thì phải đồng thời sửa contract và Go. Không hiển thị bộ lọc như đã áp dụng nếu server chưa xử lý.
2. Khi gửi request: hiện spinner; kết quả rỗng có `EmptyState`; lỗi kết nối có nút Thử lại. Render kết quả bằng `FlightRow` có giá, giờ, hãng, hạng theo response thật.
3. Chọn dòng → `FLIGHT_GET` → chi tiết. So sánh giá/hãng là bước sau khi backend có dữ liệu và message phù hợp.

### 6.4. Đặt chỗ, ghế, thanh toán giả lập

1. Tạo `BookingDraft` giữ chuyến, liên hệ, hành khách, ghế và tổng tiền **server trả về**. App có thể hiển thị ước tính, nhưng giá cuối do Go xác nhận.
2. Dựng wizard 5 bước theo `check-in/index.jsx`. Sơ đồ ghế phải lấy trạng thái từ server; trang web hiện có đoạn sinh ghế ngẫu nhiên, **không chuyển logic này sang GTK4**.
3. Khi giữ ghế, hiển thị thời hạn giữ và cập nhật khi server trả `SEAT_TAKEN`/hết hạn. Nút thanh toán gửi lệnh một lần với `req_id` ổn định; không tự gửi lại vô điều kiện khi timeout.
4. Thanh toán mô phỏng đúng ghi chú của GV. Không dựng form nhập số thẻ/CVV thật; thành phần Stripe web không được dùng trong app GTK4.

### 6.5. Vé, tài khoản, thông báo

1. Màn vé hiển thị danh sách/tra cứu bằng PNR và thông tin xác minh, chi tiết vé, trạng thái chuyến và hủy nếu backend đã có handler. PDF/QR/đổi vé là phần mở rộng sau MVP.
2. Màn tài khoản dùng các form hồ sơ, lịch sử, đổi mật khẩu chỉ khi Go bridge có message tương ứng.
3. Thông báo realtime: client C nhận frame push ở worker → chuyển về main context → cập nhật danh sách/nhãn trong UI. C gateway hiện chưa hoàn thiện push nên đây là việc cần làm ở server, không chỉ UI.

## 7. Quy tắc thiết kế giao diện

- Giữ nhãn tiếng Việt rõ, thứ tự tab hợp lý, `GtkLabel` liên kết trường nhập, focus hiển thị được; dùng bàn phím để hoàn thành form và kích hoạt nút chính.
- Mỗi request có 4 trạng thái nhìn thấy: **chưa gửi**, **đang xử lý**, **thành công/rỗng**, **lỗi**. Tránh spinner chạy mãi khi mất mạng.
- Dùng kích thước cửa sổ mặc định phù hợp laptop, hỗ trợ resize; danh sách dài đặt trong `GtkScrolledWindow`.
- Dùng ảnh từ `frontend/public` hoặc `frontend/src/assets` nếu giấy phép/định dạng phù hợp; nén và gắn bằng GResource. Không phụ thuộc đường dẫn tuyệt đối trên máy lập trình.
- Không sao chép bố cục marketing của trang chủ web một cách máy móc. App desktop nên ưu tiên form tìm chuyến và đường đi tới vé đã đặt; các phần tin tức/đánh giá chỉ làm khi còn phạm vi.

## 8. Thứ tự bàn giao UI

| Mốc | Màn hoàn thành | Điều kiện xong |
|---|---|---|
| UI-1 | Khung app, home tối giản, trạng thái kết nối | Health thật qua TCP; UI không treo khi server tắt |
| UI-2 | Login/signup | Auth thật, báo lỗi và điều hướng đúng |
| UI-3 | Tìm kiếm/danh sách/chi tiết | Dữ liệu từ DB, có empty/loading/error |
| UI-4 | Wizard đặt chỗ/thanh toán giả lập | Ghế và giá do server xác nhận; không nhân đôi booking |
| UI-5 | Vé/tài khoản/thông báo | Chỉ bật hành động khi backend hỗ trợ; push hiện ngay |
| UI-6 | Đóng gói Windows | `.exe` cùng GTK runtime và assets chạy trên máy sạch |

**Phân công theo kế hoạch nhóm:** Nguyễn Đức Hiếu chủ trì khung GTK4, login/signup, tìm kiếm; Nguyễn Tuấn Anh chủ trì chọn ghế, booking, thanh toán mô phỏng, vé/thông báo. Cả hai thống nhất component/style và cùng kiểm tra đường dữ liệu thật.

## 9. Tài liệu GTK dùng khi triển khai

- [GTK4 Getting Started](https://docs.gtk.org/gtk4/getting_started.html): `GtkApplication`, `GtkBuilder`, resource.
- [GtkStack](https://docs.gtk.org/gtk4/class.Stack.html): chuyển trang và các bước wizard.
- [GtkCssProvider](https://docs.gtk.org/gtk4/class.CssProvider.html): nạp CSS GTK.
- [GTK threading](https://docs.gtk.org/gtk4/section-threading.html): trả kết quả mạng về main thread.
- [GTK trên Windows](https://www.gtk.org/docs/installations/windows): môi trường MSYS2 và đóng gói runtime.
