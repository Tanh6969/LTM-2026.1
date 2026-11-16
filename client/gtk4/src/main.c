#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "net_client.h"

#ifdef HAVE_GST
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/app/gstappsink.h>
#endif

enum { MSG_HEALTH = 0x0001, MSG_REGISTER = 0x0101, MSG_LOGIN = 0x0102,
       MSG_LOGOUT = 0x0103, MSG_SEARCH = 0x0201, MSG_FLIGHT_GET = 0x0202,
       MSG_FLIGHT_LIST = 0x0203 };

typedef struct App App;
typedef struct Task Task;
typedef void (*ReplyHandler)(App *, const NetReply *);

struct App {
    GtkWidget *window, *stack, *nav, *status, *welcome, *results, *detail;
    GtkWidget *hero_stack, *hero_indicator, *home_scroll;
    guint hero_timer;
    guint hero_index;
    GtkWidget *login_email, *login_password;
    GtkWidget *reg_first, *reg_last, *reg_email, *reg_password;
    GtkWidget *from, *to, *date;
    GAsyncQueue *queue;
    NetClient *network;
    GThread *worker;
    gboolean logged_in;
#ifdef HAVE_GST
    GtkWidget  *hero_video_picture; /* GtkPicture updated each video frame */
    GstElement *video_pipeline;
    guint       video_bus_watch;
#endif
};

struct Task {
    App *app;
    guint16 type;
    char *json;
    ReplyHandler handler;
    NetReply reply;
};

static void show_page(App *app, const char *page) {
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), page);
    if (!app->nav) return;
    for (GtkWidget *child = gtk_widget_get_first_child(app->nav); child;
         child = gtk_widget_get_next_sibling(child)) {
        const char *target = g_object_get_data(G_OBJECT(child), "page");
        if (g_strcmp0(target, page) == 0) gtk_widget_add_css_class(child, "nav-active");
        else gtk_widget_remove_css_class(child, "nav-active");
    }
}

static void set_status(App *app, const char *message) {
    gtk_label_set_text(GTK_LABEL(app->status), message);
}

static const char *json_string(JsonObject *obj, const char *name) {
    if (!obj || !json_object_has_member(obj, name)) return "";
    JsonNode *node = json_object_get_member(obj, name);
    return JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING
        ? json_node_get_string(node) : "";
}

static JsonObject *parse_object(const char *raw, JsonParser **parser_out) {
    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    if (!raw || !json_parser_load_from_data(parser, raw, -1, &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) { g_object_unref(parser); return NULL; }
    *parser_out = parser;
    return json_node_get_object(root);
}

static char *response_error(const NetReply *reply) {
    if (reply->error) return g_strdup(reply->error);
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(reply->body, &parser);
    const char *message = obj ? json_string(obj, "message") : "";
    char *text = *message ? g_strdup(message) : g_strdup_printf("Server trả lỗi %u.", reply->status);
    if (parser) g_object_unref(parser);
    return text;
}

static gboolean deliver_result(gpointer data) {
    Task *task = data;
    if (task->reply.error && task->app->logged_in) {
        task->app->logged_in = FALSE;
        gtk_label_set_text(GTK_LABEL(task->app->welcome),
                           "Kết nối đã mất. Hãy đăng nhập lại sau khi kết nối.");
    }
    task->handler(task->app, &task->reply);
    net_reply_clear(&task->reply);
    g_free(task->json);
    g_free(task);
    return G_SOURCE_REMOVE;
}

static gpointer network_worker(gpointer data) {
    App *app = data;
    for (;;) {
        Task *task = g_async_queue_pop(app->queue);
        if (!task) continue;
        if (!task->handler) { g_free(task); break; }
        task->reply = net_client_request(app->network, task->type, task->json);
        g_main_context_invoke(NULL, deliver_result, task);
    }
    return NULL;
}

static void request(App *app, guint16 type, char *json, ReplyHandler handler) {
    Task *task = g_new0(Task, 1);
    task->app = app;
    task->type = type;
    task->json = json;
    task->handler = handler;
    g_async_queue_push(app->queue, task);
}

static char *make_json(JsonBuilder *builder) {
    JsonNode *node = json_builder_get_root(builder);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, node);
    char *text = json_generator_to_data(generator, NULL);
    g_object_unref(generator);
    json_node_free(node);
    g_object_unref(builder);
    return text;
}

static JsonBuilder *object_builder(void) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    return builder;
}

static void add_string(JsonBuilder *builder, const char *name, const char *value) {
    json_builder_set_member_name(builder, name);
    json_builder_add_string_value(builder, value);
}

static void show_health(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        char *message = response_error(reply);
        set_status(app, message); g_free(message); return;
    }
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(reply->body, &parser);
    const char *state = obj ? json_string(obj, "status") : "";
    char *message = g_strdup_printf("Đã kết nối server%s%s", *state ? ": " : ".", state);
    set_status(app, message);
    g_free(message);
    if (parser) g_object_unref(parser);
}

static void health_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    set_status(app, "Đang kiểm tra kết nối...");
    request(app, MSG_HEALTH, g_strdup("{}"), show_health);
}

static void show_login(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        char *message = response_error(reply);
        set_status(app, message); g_free(message); return;
    }
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(reply->body, &parser);
    if (!obj || !json_object_has_member(obj, "user")) {
        set_status(app, "Phản hồi đăng nhập không hợp lệ.");
        if (parser) g_object_unref(parser);
        return;
    }
    JsonObject *user = obj && json_object_has_member(obj, "user")
        ? json_object_get_object_member(obj, "user") : NULL;
    const char *name = json_string(user, "name");
    char *welcome = g_strdup_printf("Xin chào %s", *name ? name : "bạn");
    gtk_label_set_text(GTK_LABEL(app->welcome), welcome);
    g_free(welcome);
    gtk_editable_set_text(GTK_EDITABLE(app->login_password), "");
    app->logged_in = TRUE;
    set_status(app, "Đăng nhập thành công.");
    show_page(app, "home");
    if (parser) g_object_unref(parser);
}

static void login_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    const char *email = gtk_editable_get_text(GTK_EDITABLE(app->login_email));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(app->login_password));
    if (!*email || !*password) { set_status(app, "Nhập email và mật khẩu."); return; }
    JsonBuilder *builder = object_builder();
    add_string(builder, "email", email); add_string(builder, "password", password);
    json_builder_end_object(builder);
    set_status(app, "Đang đăng nhập...");
    request(app, MSG_LOGIN, make_json(builder), show_login);
}

static void show_register(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        char *message = response_error(reply);
        set_status(app, message); g_free(message); return;
    }
    gtk_editable_set_text(GTK_EDITABLE(app->reg_password), "");
    set_status(app, "Đăng ký thành công. Hãy đăng nhập.");
    show_page(app, "login");
}

static void register_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    const char *first = gtk_editable_get_text(GTK_EDITABLE(app->reg_first));
    const char *last = gtk_editable_get_text(GTK_EDITABLE(app->reg_last));
    const char *email = gtk_editable_get_text(GTK_EDITABLE(app->reg_email));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(app->reg_password));
    if (!*first || !*last || !*email || !*password) { set_status(app, "Điền đủ thông tin đăng ký."); return; }
    JsonBuilder *builder = object_builder();
    add_string(builder, "first_name", first); add_string(builder, "last_name", last);
    add_string(builder, "email", email); add_string(builder, "password", password);
    json_builder_end_object(builder);
    set_status(app, "Đang đăng ký...");
    request(app, MSG_REGISTER, make_json(builder), show_register);
}

static void clear_results(App *app) {
    GtkWidget *row;
    while ((row = gtk_widget_get_first_child(app->results)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(app->results), row);
}

static void flight_detail_result(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        char *message = response_error(reply);
        set_status(app, message); g_free(message); return;
    }
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(reply->body, &parser);
    if (obj) {
        char *summary = g_strdup_printf("Chuyến %s\n%s → %s\nTrạng thái: %s",
            json_string(obj, "flightNumber"), json_string(obj, "departureCity"),
            json_string(obj, "arrivalCity"), json_string(obj, "status"));
        gtk_label_set_text(GTK_LABEL(app->detail), summary);
        g_free(summary);
        show_page(app, "detail");
        set_status(app, "Đã tải chi tiết chuyến bay.");
    } else set_status(app, "Phản hồi chi tiết chuyến bay không hợp lệ.");
    if (parser) g_object_unref(parser);
}

static void add_flight_row(App *app, const char *id, const char *number,
                           const char *airline, const char *from, const char *to,
                           const char *time, gint64 price) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(info, TRUE);

    char *heading = g_strdup_printf("%s  ·  %s", number, airline);
    GtkWidget *name = gtk_label_new(heading);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_widget_add_css_class(name, "flight-number");
    gtk_box_append(GTK_BOX(info), name);

    char *route = g_strdup_printf("%s  →  %s", from, to);
    GtkWidget *route_label = gtk_label_new(route);
    gtk_label_set_xalign(GTK_LABEL(route_label), 0);
    gtk_widget_add_css_class(route_label, "flight-route");
    gtk_box_append(GTK_BOX(info), route_label);

    GtkWidget *departure = gtk_label_new(time);
    gtk_label_set_xalign(GTK_LABEL(departure), 0);
    gtk_widget_add_css_class(departure, "muted");
    gtk_box_append(GTK_BOX(info), departure);

    gtk_box_append(GTK_BOX(layout), info);

    char *price_text = g_strdup_printf("Từ %" G_GINT64_FORMAT " đ", price);
    GtkWidget *price_label = gtk_label_new(price_text);
    gtk_widget_add_css_class(price_label, "flight-price");
    gtk_widget_set_valign(price_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(layout), price_label);

    GtkWidget *choose_btn = gtk_button_new_with_label("Chọn vé  →");
    gtk_widget_add_css_class(choose_btn, "primary-button");
    gtk_widget_set_valign(choose_btn, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(layout), choose_btn);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), layout);
    g_object_set_data_full(G_OBJECT(row), "flight-id", g_strdup(id), g_free);
    gtk_list_box_append(GTK_LIST_BOX(app->results), row);

    g_free(heading);
    g_free(route);
    g_free(price_text);
}

static void populate_mock_flights(App *app, const char *from, const char *to, const char *date) {
    clear_results(app);
    char t1[96], t2[96], t3[96], t4[96];
    g_snprintf(t1, sizeof(t1), "%s  ·  08:30 - 10:45", date);
    g_snprintf(t2, sizeof(t2), "%s  ·  11:15 - 13:25", date);
    g_snprintf(t3, sizeof(t3), "%s  ·  14:00 - 16:15", date);
    g_snprintf(t4, sizeof(t4), "%s  ·  17:45 - 20:00", date);

    add_flight_row(app, "VN-245", "VN-245", "Vietnam Airlines", from, to, t1, 1850000);
    add_flight_row(app, "VJ-134", "VJ-134", "Vietjet Air", from, to, t2, 1290000);
    add_flight_row(app, "QH-202", "QH-202", "Bamboo Airways", from, to, t3, 1650000);
    add_flight_row(app, "VU-331", "VU-331", "Vietravel Airlines", from, to, t4, 1150000);
}

static void selected_flight(GtkListBox *list, GtkListBoxRow *row, gpointer data) {
    (void)list;
    if (!row) return;
    App *app = data;
    const char *flight_id = g_object_get_data(G_OBJECT(row), "flight-id");
    if (!flight_id || !*flight_id) return;

    const char *from = gtk_editable_get_text(GTK_EDITABLE(app->from));
    const char *to = gtk_editable_get_text(GTK_EDITABLE(app->to));
    const char *date = gtk_editable_get_text(GTK_EDITABLE(app->date));

    char *info_text = g_strdup_printf(
        "MÃ CHUYẾN BAY: %s\n\n"
        "✦ Tuyến bay: %s  →  %s\n"
        "✦ Ngày khởi hành: %s\n"
        "✦ Hạng dịch vụ: Phổ thông linh hoạt (Economy Flex)\n"
        "✦ Tiêu chuẩn hành lý: 12kg xách tay + 23kg ký gửi\n"
        "✦ Trạng thái: Còn 18 ghế trống\n"
        "✦ Suất ăn: Đã bao gồm bữa ăn nóng & đồ uống\n\n"
        "Bấm 'Vé của tôi' để theo dõi vé sau khi hoàn tất đặt vé.",
        flight_id, *from ? from : "Điểm đi", *to ? to : "Điểm đến", *date ? date : "2026-11-17");
    gtk_label_set_text(GTK_LABEL(app->detail), info_text);
    g_free(info_text);
    show_page(app, "detail");

    JsonBuilder *builder = object_builder();
    add_string(builder, "flightId", flight_id);
    json_builder_end_object(builder);
    request(app, MSG_FLIGHT_GET, make_json(builder), flight_detail_result);
}

static void show_flights(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        return;
    }
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(reply->body, &parser);
    JsonNode *data = obj && json_object_has_member(obj, "data")
        ? json_object_get_member(obj, "data") : NULL;
    JsonArray *items = data && JSON_NODE_HOLDS_ARRAY(data) ? json_node_get_array(data) : NULL;
    if (!items) {
        if (parser) g_object_unref(parser);
        return;
    }
    clear_results(app);
    guint count = json_array_get_length(items);
    for (guint i = 0; i < count; i++) {
        JsonObject *flight = json_array_get_object_element(items, i);
        if (!flight) continue;
        const char *id = json_string(flight, "flightId");
        const char *number = json_string(flight, "flightNumber");
        const char *airline = json_string(flight, "airline");
        const char *from = json_string(flight, "departureCity");
        const char *to = json_string(flight, "arrivalCity");
        const char *time = json_string(flight, "departureTime");
        gint64 price = json_object_has_member(flight, "basePrice")
            ? json_object_get_int_member(flight, "basePrice") : 0;
        add_flight_row(app, id, number, airline, from, to, time, price);
    }
    set_status(app, count ? "Đã cập nhật chuyến bay từ server." : "Không có chuyến bay từ server.");
    if (parser) g_object_unref(parser);
}

static void search_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    const char *from = gtk_editable_get_text(GTK_EDITABLE(app->from));
    const char *to = gtk_editable_get_text(GTK_EDITABLE(app->to));
    const char *date = gtk_editable_get_text(GTK_EDITABLE(app->date));
    if (!*from || !*to || !*date || strcmp(from, to) == 0) {
        set_status(app, "Vui lòng nhập điểm đi và điểm đến khác nhau.");
        return;
    }

    populate_mock_flights(app, from, to, date);
    show_page(app, "flights");
    set_status(app, "Chọn một chuyến bay để xem chi tiết hành trình.");

    JsonBuilder *builder = object_builder();
    add_string(builder, "departureCity", from);
    add_string(builder, "arrivalCity", to);
    add_string(builder, "flightDate", date);
    json_builder_end_object(builder);
    request(app, MSG_SEARCH, make_json(builder), show_flights);
}

static void list_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    const char *from = gtk_editable_get_text(GTK_EDITABLE(app->from));
    const char *to = gtk_editable_get_text(GTK_EDITABLE(app->to));
    const char *date = gtk_editable_get_text(GTK_EDITABLE(app->date));
    populate_mock_flights(app,
        (*from && *from != ' ') ? from : "Hà Nội",
        (*to && *to != ' ') ? to : "TP. Hồ Chí Minh",
        (*date && *date != ' ') ? date : "2026-11-17");
    show_page(app, "flights");
    set_status(app, "Danh sách chuyến bay gợi ý.");
    request(app, MSG_FLIGHT_LIST, g_strdup("{\"page\":1,\"limit\":10}"), show_flights);
}

static void add_class(GtkWidget *widget, const char *name) {
    gtk_widget_add_css_class(widget, name);
}

static GtkWidget *ui_label(const char *text, const char *css_class) {
    GtkWidget *widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_label_set_wrap(GTK_LABEL(widget), TRUE);
    if (css_class) add_class(widget, css_class);
    return widget;
}

static GtkWidget *ui_button(const char *text, const char *css_class,
                            GCallback callback, gpointer user_data) {
    GtkWidget *button = gtk_button_new_with_label(text);
    if (css_class) add_class(button, css_class);
    if (callback) g_signal_connect(button, "clicked", callback, user_data);
    return button;
}

static void ui_nav_clicked(GtkButton *button, gpointer data) {
    App *app = data;
    show_page(app, g_object_get_data(G_OBJECT(button), "page"));
}

static void ui_nav_add(GtkWidget *box, App *app, const char *text,
                       const char *page, gboolean prominent) {
    GtkWidget *button = ui_button(text, prominent ? "nav-primary" : "nav-link",
                                  G_CALLBACK(ui_nav_clicked), app);
    g_object_set_data(G_OBJECT(button), "page", (gpointer)page);
    gtk_box_append(GTK_BOX(box), button);
}

static GtkWidget *ui_field(GtkWidget *parent, const char *title,
                           const char *placeholder, GtkWidget **entry,
                           gboolean password) {
    GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(group, TRUE);
    gtk_box_append(GTK_BOX(group), ui_label(title, "field-caption"));
    *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(*entry), placeholder);
    gtk_entry_set_visibility(GTK_ENTRY(*entry), !password);
    add_class(*entry, "form-entry");
    gtk_box_append(GTK_BOX(group), *entry);
    gtk_box_append(GTK_BOX(parent), group);
    return group;
}

static void on_calendar_day_selected(GtkCalendar *cal, gpointer user_data) {
    GtkWidget *popover = g_object_get_data(G_OBJECT(cal), "popover");
    GtkEditable *entry = user_data;
    GDateTime *dt = gtk_calendar_get_date(cal);
    if (dt) {
        char *formatted = g_date_time_format(dt, "%Y-%m-%d");
        gtk_editable_set_text(entry, formatted);
        g_free(formatted);
        g_date_time_unref(dt);
    }
    if (popover) gtk_popover_popdown(GTK_POPOVER(popover));
}

static void on_calendar_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWidget *popover = user_data;
    gtk_popover_popup(GTK_POPOVER(popover));
}

static GtkWidget *ui_date_field(const char *title, GtkWidget **entry_out) {
    GtkWidget *field = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    add_class(field, "search-field");
    gtk_widget_set_hexpand(field, TRUE);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(content, TRUE);
    gtk_box_append(GTK_BOX(content), ui_label(title, "search-caption"));

    GtkWidget *entry = gtk_entry_new();
    GDateTime *now = g_date_time_new_now_local();
    GDateTime *tomorrow = g_date_time_add_days(now, 1);
    char *def_date = g_date_time_format(tomorrow, "%Y-%m-%d");
    gtk_editable_set_text(GTK_EDITABLE(entry), def_date);
    g_free(def_date);
    g_date_time_unref(now);
    g_date_time_unref(tomorrow);

    gtk_entry_set_has_frame(GTK_ENTRY(entry), FALSE);
    add_class(entry, "search-entry");
    gtk_box_append(GTK_BOX(content), entry);
    *entry_out = entry;

    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, field);
    GtkWidget *cal = gtk_calendar_new();
    g_object_set_data(G_OBJECT(cal), "popover", popover);
    g_signal_connect(cal, "day-selected", G_CALLBACK(on_calendar_day_selected), entry);
    gtk_popover_set_child(GTK_POPOVER(popover), cal);

    GtkWidget *icon_btn = gtk_button_new();
    add_class(icon_btn, "search-icon-btn");
    gtk_button_set_child(GTK_BUTTON(icon_btn),
        gtk_image_new_from_resource("/vn/ltm/flight/calendar.svg"));
    gtk_widget_set_valign(icon_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(icon_btn, "Bấm để chọn ngày");
    g_signal_connect(icon_btn, "clicked", G_CALLBACK(on_calendar_btn_clicked), popover);

    gtk_box_append(GTK_BOX(field), icon_btn);
    gtk_box_append(GTK_BOX(field), content);
    return field;
}

static void on_airport_chosen(GtkButton *btn, gpointer user_data) {
    GtkWidget *popover = g_object_get_data(G_OBJECT(btn), "popover");
    GtkEditable *entry = user_data;
    const char *val = g_object_get_data(G_OBJECT(btn), "airport_val");
    if (val && entry) gtk_editable_set_text(entry, val);
    if (popover) gtk_popover_popdown(GTK_POPOVER(popover));
}

static void on_airport_picker_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWidget *popover = user_data;
    gtk_popover_popup(GTK_POPOVER(popover));
}

static GtkWidget *ui_airport_field(const char *title, const char *default_val,
                                  const char *icon_path, GtkWidget **entry_out) {
    GtkWidget *field = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    add_class(field, "search-field");
    gtk_widget_set_hexpand(field, TRUE);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(content, TRUE);
    gtk_box_append(GTK_BOX(content), ui_label(title, "search-caption"));

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), default_val);
    gtk_entry_set_has_frame(GTK_ENTRY(entry), FALSE);
    add_class(entry, "search-entry");
    gtk_box_append(GTK_BOX(content), entry);
    *entry_out = entry;

    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, field);

    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(list_box, 6);
    gtk_widget_set_margin_end(list_box, 6);
    gtk_widget_set_margin_top(list_box, 6);
    gtk_widget_set_margin_bottom(list_box, 6);

    const char *airports[] = {
        "Hà Nội (HAN)", "TP. Hồ Chí Minh (SGN)", "Đà Nẵng (DAD)",
        "Nha Trang (CXR)", "Phú Quốc (PQC)", "Đà Lạt (DLI)",
        "Hải Phòng (HPH)", "Huế (HUI)", "Bangkok (BKK)"
    };
    for (guint i = 0; i < G_N_ELEMENTS(airports); i++) {
        GtkWidget *item = ui_button(airports[i], "airport-popover-item",
                                    G_CALLBACK(on_airport_chosen), entry);
        g_object_set_data(G_OBJECT(item), "airport_val", (gpointer)airports[i]);
        g_object_set_data(G_OBJECT(item), "popover", popover);
        gtk_box_append(GTK_BOX(list_box), item);
    }
    gtk_popover_set_child(GTK_POPOVER(popover), list_box);

    GtkWidget *icon_btn = gtk_button_new();
    add_class(icon_btn, "search-icon-btn");
    gtk_button_set_child(GTK_BUTTON(icon_btn),
        gtk_image_new_from_resource(icon_path));
    gtk_widget_set_valign(icon_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(icon_btn, "Bấm để chọn nhanh sân bay");
    g_signal_connect(icon_btn, "clicked", G_CALLBACK(on_airport_picker_clicked), popover);

    gtk_box_append(GTK_BOX(field), icon_btn);
    gtk_box_append(GTK_BOX(field), content);
    return field;
}

static void ui_swap_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    char *from = g_strdup(gtk_editable_get_text(GTK_EDITABLE(app->from)));
    char *to = g_strdup(gtk_editable_get_text(GTK_EDITABLE(app->to)));
    gtk_editable_set_text(GTK_EDITABLE(app->from), to);
    gtk_editable_set_text(GTK_EDITABLE(app->to), from);
    g_free(from);
    g_free(to);
}

static GtkWidget *ui_search_submit(App *app) {
    GtkWidget *button = gtk_button_new();
    add_class(button, "search-submit");
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), gtk_image_new_from_resource("/vn/ltm/flight/search.svg"));
    gtk_box_append(GTK_BOX(content), ui_label("TÌM CHUYẾN", NULL));
    gtk_button_set_child(GTK_BUTTON(button), content);
    g_signal_connect(button, "clicked", G_CALLBACK(search_clicked), app);
    return button;
}

static GtkWidget *ui_feature(const char *icon, const char *title) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    add_class(card, "feature-card");
    gtk_widget_set_hexpand(card, TRUE);
    gtk_widget_set_halign(card, GTK_ALIGN_FILL);

    GtkWidget *icon_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(icon_box, "feature-icon-wrapper");
    gtk_widget_set_size_request(icon_box, 64, 64);
    gtk_widget_set_halign(icon_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon_box, GTK_ALIGN_CENTER);

    GtkWidget *icon_lbl = ui_label(icon, "feature-icon-symbol");
    gtk_widget_set_halign(icon_lbl, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon_lbl, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(icon_lbl, TRUE);
    gtk_box_append(GTK_BOX(icon_box), icon_lbl);

    GtkWidget *title_lbl = ui_label(title, "feature-title");
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_CENTER);

    gtk_box_append(GTK_BOX(card), icon_box);
    gtk_box_append(GTK_BOX(card), title_lbl);
    return card;
}

static GtkWidget *ui_about_tile(const char *symbol, const char *title) {
    GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(tile, "about-tile");
    gtk_widget_set_hexpand(tile, TRUE);
    gtk_box_append(GTK_BOX(tile), ui_label(symbol, "about-icon"));
    gtk_box_append(GTK_BOX(tile), ui_label(title, "about-tile-title"));
    return tile;
}

static GtkWidget *ui_picture(const char *filename) {
    char *path = g_strdup_printf("/vn/ltm/flight/%s", filename);
    GtkWidget *picture = gtk_picture_new_for_resource(path);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_COVER);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    g_free(path);
    return picture;
}

#ifndef HAVE_GST
static GtkWidget *ui_hero_picture(const char *filename) {
    char *path = g_strdup_printf("/vn/ltm/flight/%s", filename);
    GError *error = NULL;
    GdkPixbuf *pixels = gdk_pixbuf_new_from_resource_at_scale(path, 1600, 900,
                                                               TRUE, &error);
    g_free(path);
    if (!pixels) {
        g_warning("Cannot prepare hero image %s: %s", filename,
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        return ui_picture(filename);
    }
    GBytes *bytes = g_bytes_new(gdk_pixbuf_get_pixels(pixels),
                                gdk_pixbuf_get_byte_length(pixels));
    GdkTexture *texture = gdk_memory_texture_new(
        gdk_pixbuf_get_width(pixels), gdk_pixbuf_get_height(pixels),
        gdk_pixbuf_get_has_alpha(pixels) ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8,
        bytes, gdk_pixbuf_get_rowstride(pixels));
    GtkWidget *picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_COVER);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    g_object_unref(texture);
    g_bytes_unref(bytes);
    g_object_unref(pixels);
    return picture;
}
#endif

static gboolean ui_animations_enabled(void) {
    gboolean enabled = TRUE;
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) g_object_get(settings, "gtk-enable-animations", &enabled, NULL);
    return enabled;
}

#ifndef HAVE_GST
static void ui_hero_step(App *app, gint direction) {
    if (!app->hero_stack) return;
    if (gtk_stack_get_transition_running(GTK_STACK(app->hero_stack))) return;
    app->hero_index = (app->hero_index + 3 + direction) % 3;
    char *slide = g_strdup_printf("scene-%u", app->hero_index);
    gtk_stack_set_visible_child_name(GTK_STACK(app->hero_stack), slide);
    g_free(slide);
    char *number = g_strdup_printf("%02u / 03", app->hero_index + 1);
    gtk_label_set_text(GTK_LABEL(app->hero_indicator), number);
    g_free(number);
}

static gboolean ui_hero_tick(gpointer data) {
    App *app = data;
    app->hero_timer = 0;
    if (!app->hero_stack) {
        return G_SOURCE_REMOVE;
    }
    if (gtk_widget_get_mapped(app->hero_stack)) ui_hero_step(app, 1);
    if (app->hero_stack && ui_animations_enabled())
        app->hero_timer = g_timeout_add_seconds(8, ui_hero_tick, app);
    return G_SOURCE_REMOVE;
}

static void ui_hero_manual(GtkButton *button, gpointer data) {
    App *app = data;
    if (app->hero_timer) {
        g_source_remove(app->hero_timer);
        app->hero_timer = 0;
    }
    gint direction = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "direction"));
    ui_hero_step(app, direction);
    if (ui_animations_enabled())
        app->hero_timer = g_timeout_add_seconds(8, ui_hero_tick, app);
}
#endif

static void ui_scroll_to_search(App *app) {
    GtkAdjustment *scroll = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(app->home_scroll));
    gtk_adjustment_set_value(scroll, gtk_adjustment_get_lower(scroll));
    gtk_widget_grab_focus(app->date);
}

static void ui_header_search_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    show_page(app, "home");
    ui_scroll_to_search(app);
    gtk_widget_grab_focus(app->from);
}

static void ui_destination_clicked(GtkButton *button, gpointer data) {
    App *app = data;
    const char *from = g_object_get_data(G_OBJECT(button), "from");
    const char *to = g_object_get_data(G_OBJECT(button), "to");
    gtk_editable_set_text(GTK_EDITABLE(app->from), from);
    gtk_editable_set_text(GTK_EDITABLE(app->to), to);
    ui_scroll_to_search(app);
    char *message = g_strdup_printf("Đã chọn %s → %s. Hãy nhập ngày bay.", from, to);
    set_status(app, message);
    g_free(message);
}

static GtkWidget *ui_destination_card(App *app, const char *image,
                                       const char *from, const char *to,
                                       const char *badge) {
    GtkWidget *button = gtk_button_new();
    add_class(button, "destination-card");
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_overflow(button, GTK_OVERFLOW_HIDDEN);
    g_object_set_data_full(G_OBJECT(button), "from", g_strdup(from), g_free);
    g_object_set_data_full(G_OBJECT(button), "to", g_strdup(to), g_free);
    g_signal_connect(button, "clicked", G_CALLBACK(ui_destination_clicked), app);
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *photo = ui_picture(image);
    gtk_widget_set_size_request(photo, 220, 275);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), photo);
    GtkWidget *caption = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(caption, "destination-caption");
    gtk_widget_set_halign(caption, GTK_ALIGN_FILL);
    gtk_widget_set_valign(caption, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(caption), ui_label(badge, "destination-badge"));
    char *route = g_strdup_printf("%s → %s", from, to);
    gtk_box_append(GTK_BOX(caption), ui_label(route, "destination-title"));
    gtk_box_append(GTK_BOX(caption), ui_label("Khám phá hành trình  →", "destination-action"));
    g_free(route);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), caption);
    gtk_button_set_child(GTK_BUTTON(button), overlay);
    return button;
}

static GtkWidget *ui_partner_logo(const char *image) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(card, "partner-logo");
    gtk_widget_set_hexpand(card, TRUE);
    GtkWidget *picture = ui_picture(image);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_size_request(picture, 140, 64);
    gtk_box_append(GTK_BOX(card), picture);
    return card;
}

/* ── GStreamer video background ─────────────────────────────────────────── */
#ifdef HAVE_GST

typedef struct { App *app; GdkTexture *texture; } TexUpdate;

/* Called on GTK main thread: swap the picture's paintable */
static gboolean video_apply_texture(gpointer data) {
    TexUpdate *tu = data;
    if (tu->app->hero_video_picture)
        gtk_picture_set_paintable(GTK_PICTURE(tu->app->hero_video_picture),
                                  GDK_PAINTABLE(tu->texture));
    g_object_unref(tu->texture);
    g_free(tu);
    return G_SOURCE_REMOVE;
}

/* Called on GStreamer thread: decode one frame → schedule GTK update */
static GstFlowReturn video_on_sample(GstAppSink *sink, gpointer data) {
    App *app = data;
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_OK;

    GstCaps   *caps   = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        GBytes     *bytes   = g_bytes_new(map.data, map.size);
        GdkTexture *texture = gdk_memory_texture_new(
            GST_VIDEO_INFO_WIDTH(&info),
            GST_VIDEO_INFO_HEIGHT(&info),
            GDK_MEMORY_R8G8B8,
            bytes,
            GST_VIDEO_INFO_PLANE_STRIDE(&info, 0));
        g_bytes_unref(bytes);
        gst_buffer_unmap(buffer, &map);

        TexUpdate *tu = g_new(TexUpdate, 1);
        tu->app     = app;
        tu->texture = texture;   /* ownership transferred */
        g_main_context_invoke(NULL, video_apply_texture, tu);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

/* GStreamer bus: handle EOS (loop) and errors */
static gboolean video_bus_cb(GstBus *bus, GstMessage *msg, gpointer data) {
    App *app = data;
    (void)bus;
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        /* Seek back to beginning for seamless looping */
        gst_element_seek_simple(app->video_pipeline, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 0);
        break;
    case GST_MESSAGE_ERROR: {
        GError *err = NULL;
        gst_message_parse_error(msg, &err, NULL);
        g_warning("GStreamer: %s", err ? err->message : "unknown error");
        g_clear_error(&err);
        break;
    }
    default: break;
    }
    return TRUE;
}

/* Find video.mp4 from several well-known relative paths */
static char *video_find_path(void) {
    const char *env = g_getenv("LTM_VIDEO");
    if (env && g_file_test(env, G_FILE_TEST_EXISTS))
        return g_canonicalize_filename(env, NULL);

    const char *candidates[] = {
        "frontend/public/video.mp4",
        "../frontend/public/video.mp4",
        "../../frontend/public/video.mp4",
        "../../../frontend/public/video.mp4",
        "../../../../frontend/public/video.mp4",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (g_file_test(candidates[i], G_FILE_TEST_EXISTS)) {
            char *abs_path = g_canonicalize_filename(candidates[i], NULL);
            g_message("Video file located at: %s", abs_path);
            return abs_path;
        }
    }
    return NULL;
}

/* Start the GStreamer pipeline using playbin with custom appsink bin */
static void video_start(App *app, const char *path) {
    char *uri = gst_filename_to_uri(path, NULL);
    if (!uri) uri = g_filename_to_uri(path, NULL, NULL);
    if (!uri) { g_warning("Cannot convert path to URI: %s", path); return; }

    app->video_pipeline = gst_element_factory_make("playbin", "video_player");
    if (!app->video_pipeline) {
        g_warning("Cannot create playbin element");
        g_free(uri);
        return;
    }
    g_object_set(app->video_pipeline, "uri", uri, "flags", 1, NULL);
    g_free(uri);

    GError *err = NULL;
    GstElement *vsink_bin = gst_parse_bin_from_description(
        "videoconvert ! video/x-raw,format=RGB ! appsink name=vsink sync=true max-buffers=2 drop=true",
        TRUE, &err);
    if (!vsink_bin || err) {
        g_warning("Cannot create vsink bin: %s", err ? err->message : "?");
        g_clear_error(&err);
        g_clear_object(&app->video_pipeline);
        return;
    }

    GstElement *vsink = gst_bin_get_by_name(GST_BIN(vsink_bin), "vsink");
    if (vsink) {
        GstAppSinkCallbacks cbs = { .new_sample = video_on_sample };
        gst_app_sink_set_callbacks(GST_APP_SINK(vsink), &cbs, app, NULL);
        gst_app_sink_set_emit_signals(GST_APP_SINK(vsink), FALSE);
        gst_object_unref(vsink);
    }

    g_object_set(app->video_pipeline, "video-sink", vsink_bin, NULL);

    GstBus *bus = gst_element_get_bus(app->video_pipeline);
    app->video_bus_watch = gst_bus_add_watch(bus, video_bus_cb, app);
    gst_object_unref(bus);

    gst_element_set_state(app->video_pipeline, GST_STATE_PLAYING);
    g_message("Video background playback successfully started.");
}

/* Stop and clean up the pipeline */
static void video_stop(App *app) {
    if (app->video_bus_watch) {
        g_source_remove(app->video_bus_watch);
        app->video_bus_watch = 0;
    }
    if (app->video_pipeline) {
        gst_element_set_state(app->video_pipeline, GST_STATE_NULL);
        g_clear_object(&app->video_pipeline);
    }
}

#endif /* HAVE_GST */
/* ─────────────────────────────────────────────────────────────────────────── */

static GtkWidget *ui_home(App *app) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    app->home_scroll = scroll;
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), page);

    GtkWidget *hero = gtk_overlay_new();
    add_class(hero, "hero");
    gtk_widget_set_size_request(hero, -1, 610);

#ifdef HAVE_GST
    /* ── Video background (GTK4 + GStreamer appsink) ── */
    app->hero_video_picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(app->hero_video_picture), GTK_CONTENT_FIT_COVER);
    gtk_picture_set_can_shrink(GTK_PICTURE(app->hero_video_picture), TRUE);
    /* Show a static fallback image immediately while video loads */
    {
        GdkTexture *fallback = gdk_texture_new_from_resource("/vn/ltm/flight/tours_background.jpg");
        if (fallback) {
            gtk_picture_set_paintable(GTK_PICTURE(app->hero_video_picture),
                                      GDK_PAINTABLE(fallback));
            g_object_unref(fallback);
        }
    }
    gtk_overlay_set_child(GTK_OVERLAY(hero), app->hero_video_picture);
    /* Start video pipeline (async — first frame will replace fallback) */
    char *vpath = video_find_path();
    if (vpath) { video_start(app, vpath); g_free(vpath); }
    else g_message("video.mp4 not found — static fallback used");
#else
    /* ── Slideshow fallback (no GStreamer) ── */
    app->hero_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app->hero_stack),
        ui_animations_enabled() ? GTK_STACK_TRANSITION_TYPE_CROSSFADE
                                : GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_set_transition_duration(GTK_STACK(app->hero_stack), 650);
    const char *scenes[] = {"tours_background.jpg", "clouds-background.jpg", "bg-1.jpg"};
    for (guint i = 0; i < G_N_ELEMENTS(scenes); i++) {
        char *name = g_strdup_printf("scene-%u", i);
        gtk_stack_add_named(GTK_STACK(app->hero_stack), ui_hero_picture(scenes[i]), name);
        g_free(name);
    }
    gtk_stack_set_visible_child_name(GTK_STACK(app->hero_stack), "scene-0");
    g_object_add_weak_pointer(G_OBJECT(app->hero_stack), (gpointer *)&app->hero_stack);
    gtk_overlay_set_child(GTK_OVERLAY(hero), app->hero_stack);
#endif /* HAVE_GST */

    GtkWidget *scrim = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(scrim, "hero-scrim");
    gtk_overlay_add_overlay(GTK_OVERLAY(hero), scrim);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    add_class(content, "hero-content");
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(content, 1020, -1);
    gtk_box_append(GTK_BOX(scrim), content);
    gtk_widget_set_vexpand(content, TRUE);
    GtkWidget *kicker = ui_label("Hãy cùng khám phá", "hero-kicker");
    gtk_widget_set_halign(kicker, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), kicker);
    GtkWidget *title = ui_label("Bạn muốn bay đến những đâu?", "hero-title");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), title);
    GtkWidget *subtitle = ui_label("Khám phá những điểm đến tuyệt đẹp cùng chúng tôi.", "hero-subtitle");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), subtitle);

    GtkWidget *search = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(search, "search-card");
    gtk_widget_set_margin_top(search, 22);
    gtk_widget_set_hexpand(search, TRUE);
    gtk_box_append(GTK_BOX(content), search);
    GtkWidget *tabs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(tabs, "search-tabs");
    GtkWidget *buy_tab = gtk_button_new_with_label("✈  MUA VÉ");
    add_class(buy_tab, "search-tab-active");
    gtk_widget_set_hexpand(buy_tab, TRUE);
    gtk_box_append(GTK_BOX(tabs), buy_tab);
    const char *pending_tabs[] = {"▣  QUẢN LÝ ĐẶT VÉ", "◉  LÀM THỦ TỤC"};
    for (guint i = 0; i < G_N_ELEMENTS(pending_tabs); i++) {
        GtkWidget *tab = gtk_button_new_with_label(pending_tabs[i]);
        add_class(tab, "search-tab-pending");
        gtk_widget_set_hexpand(tab, TRUE);
        gtk_widget_set_sensitive(tab, FALSE);
        gtk_widget_set_tooltip_text(tab, "Nghiệp vụ này chưa được nối vào server.");
        gtk_box_append(GTK_BOX(tabs), tab);
    }
    gtk_box_append(GTK_BOX(search), tabs);
    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_class(form, "search-form");
    gtk_box_append(GTK_BOX(search), form);
    gtk_box_append(GTK_BOX(form), ui_airport_field("Từ", "Hà Nội (HAN)",
        "/vn/ltm/flight/pin.svg", &app->from));
    GtkWidget *swap = gtk_button_new();
    add_class(swap, "swap-button");
    gtk_button_set_child(GTK_BUTTON(swap),
        gtk_image_new_from_resource("/vn/ltm/flight/swap.svg"));
    gtk_widget_set_tooltip_text(swap, "Đổi điểm đi và điểm đến");
    g_signal_connect(swap, "clicked", G_CALLBACK(ui_swap_clicked), app);
    gtk_box_append(GTK_BOX(form), swap);
    gtk_box_append(GTK_BOX(form), ui_airport_field("Đến", "TP. Hồ Chí Minh (SGN)",
        "/vn/ltm/flight/pin.svg", &app->to));
    gtk_box_append(GTK_BOX(form), ui_date_field("Ngày đi", &app->date));
    gtk_box_append(GTK_BOX(form), ui_search_submit(app));
    GtkWidget *search_footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_class(search_footer, "search-footer");
    gtk_box_append(GTK_BOX(search_footer), ui_label("◉  Một chiều", "trip-type"));
    GtkWidget *search_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(search_spacer, TRUE);
    gtk_box_append(GTK_BOX(search_footer), search_spacer);
    gtk_box_append(GTK_BOX(search_footer), ui_button("Xem chuyến gợi ý  →", "text-button",
                                                     G_CALLBACK(list_clicked), app));
    gtk_box_append(GTK_BOX(search), search_footer);
#ifndef HAVE_GST
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    add_class(controls, "hero-controls");
    gtk_widget_set_halign(controls, GTK_ALIGN_END);
    GtkWidget *previous = ui_button("‹", "hero-arrow", G_CALLBACK(ui_hero_manual), app);
    GtkWidget *next = ui_button("›", "hero-arrow", G_CALLBACK(ui_hero_manual), app);
    g_object_set_data(G_OBJECT(previous), "direction", GINT_TO_POINTER(-1));
    g_object_set_data(G_OBJECT(next), "direction", GINT_TO_POINTER(1));
    app->hero_indicator = ui_label("01 / 03", "hero-indicator");
    gtk_widget_set_valign(app->hero_indicator, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(controls), previous);
    gtk_box_append(GTK_BOX(controls), app->hero_indicator);
    gtk_box_append(GTK_BOX(controls), next);
    gtk_box_append(GTK_BOX(scrim), controls);
#endif
    gtk_box_append(GTK_BOX(page), hero);

    GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(section, "section");
    gtk_box_append(GTK_BOX(section), ui_label("TRẢI NGHIỆM CỦA BẠN", "section-kicker"));
    gtk_box_append(GTK_BOX(section), ui_label("Dịch vụ cho hành trình thuận tiện hơn", "section-title"));
    GtkWidget *cards = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_margin_top(cards, 12);
    gtk_box_append(GTK_BOX(cards), ui_feature("✦", "Nâng cấp vé"));
    gtk_box_append(GTK_BOX(cards), ui_feature("✈", "Chọn chỗ ngồi"));
    gtk_box_append(GTK_BOX(cards), ui_feature("❖", "Mua sắm"));
    gtk_box_append(GTK_BOX(cards), ui_feature("⌂", "Khách sạn"));
    gtk_box_append(GTK_BOX(cards), ui_feature("⚙", "Dịch vụ khác"));
    gtk_box_append(GTK_BOX(section), cards);
    gtk_box_append(GTK_BOX(page), section);

    GtkWidget *popular = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    add_class(popular, "section");
    GtkWidget *popular_kicker = ui_label("ĐIỂM ĐẾN NỔI BẬT", "section-kicker");
    gtk_widget_set_halign(popular_kicker, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(popular), popular_kicker);
    GtkWidget *popular_title = ui_label("Các Chuyến Bay Phổ Biến Nhất", "popular-title");
    gtk_widget_set_halign(popular_title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(popular), popular_title);
    GtkWidget *popular_hint = ui_label(
        "Chọn một điểm đến để điền nhanh hành trình. Giá và lịch bay sẽ lấy từ server khi bạn tìm kiếm.", "muted");
    gtk_widget_set_halign(popular_hint, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(popular), popular_hint);
    GtkWidget *destinations = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_top(destinations, 12);
    gtk_box_append(GTK_BOX(destinations), ui_destination_card(app, "golden_bridge.jpg",
        "Hà Nội", "Đà Nẵng", "PHỔ BIẾN"));
    gtk_box_append(GTK_BOX(destinations), ui_destination_card(app, "phu-quoc.jpg",
        "TP. Hồ Chí Minh", "Phú Quốc", "BIỂN ĐẢO"));
    gtk_box_append(GTK_BOX(destinations), ui_destination_card(app, "quy-nhon.jpg",
        "Hà Nội", "Quy Nhơn", "KHÁM PHÁ"));
    gtk_box_append(GTK_BOX(destinations), ui_destination_card(app, "bangkok.jpg",
        "TP. Hồ Chí Minh", "Bangkok", "QUỐC TẾ"));
    gtk_box_append(GTK_BOX(popular), destinations);
    gtk_box_append(GTK_BOX(page), popular);

    GtkWidget *introduction = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 40);
    add_class(introduction, "intro-section");
    GtkWidget *intro_image = ui_picture("image-6.jpg");
    gtk_widget_set_size_request(intro_image, 380, 350);
    gtk_widget_set_hexpand(intro_image, TRUE);
    gtk_box_append(GTK_BOX(introduction), intro_image);
    GtkWidget *intro_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_hexpand(intro_text, TRUE);
    gtk_widget_set_valign(intro_text, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(intro_text), ui_label("HÃY ĐẾN VỚI CHÚNG TÔI", "section-kicker"));
    gtk_box_append(GTK_BOX(intro_text), ui_label(
        "Tận hưởng chuyến bay của bạn cùng QAirline", "section-title"));
    gtk_box_append(GTK_BOX(intro_text), ui_label(
        "Tìm hành trình phù hợp, xem giá và thông tin chuyến bay ngay trên ứng dụng desktop.", "muted"));
    gtk_box_append(GTK_BOX(intro_text), ui_label("✓  Tìm kiếm theo điểm đi, điểm đến và ngày bay", "intro-point"));
    gtk_box_append(GTK_BOX(intro_text), ui_label("✓  Xem chi tiết trước khi quyết định", "intro-point"));
    GtkWidget *intro_cta = ui_button("KHÁM PHÁ CHUYẾN BAY  →", "primary-button",
                                      G_CALLBACK(list_clicked), app);
    gtk_widget_set_halign(intro_cta, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(intro_text), intro_cta);
    gtk_box_append(GTK_BOX(introduction), intro_text);
    gtk_box_append(GTK_BOX(page), introduction);

    GtkWidget *about = gtk_overlay_new();
    add_class(about, "about-section");
    gtk_widget_set_size_request(about, -1, 390);
    gtk_overlay_set_child(GTK_OVERLAY(about), ui_picture("bg-1.jpg"));
    GtkWidget *about_row = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(about_row), 48);
    add_class(about_row, "about-scrim");
    gtk_overlay_add_overlay(GTK_OVERLAY(about), about_row);
    GtkWidget *about_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_hexpand(about_copy, TRUE);
    gtk_widget_set_valign(about_copy, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(about_copy), ui_label("BẠN ĐÃ SẴN SÀNG ĐỂ BAY CHƯA?", "about-kicker"));
    gtk_box_append(GTK_BOX(about_copy), ui_label("Khám phá chuyến bay phù hợp ngay hôm nay", "about-title"));
    gtk_box_append(GTK_BOX(about_copy), ui_label(
        "Tìm kiếm nhanh, xem giá và thông tin chuyến bay qua kết nối TCP trực tiếp.", "about-text"));
    gtk_grid_attach(GTK_GRID(about_row), about_copy, 0, 0, 1, 1);
    GtkWidget *about_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(about_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(about_grid), 12);
    gtk_widget_set_hexpand(about_grid, TRUE);
    gtk_widget_set_valign(about_grid, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(about_grid), ui_about_tile("✈", "Tìm chuyến bay"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(about_grid), ui_about_tile("◉", "Xem giá vé"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(about_grid), ui_about_tile("⌖", "Khám phá điểm đến"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(about_grid), ui_about_tile("✓", "Tra cứu chi tiết"), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(about_row), about_grid, 1, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), about);

    GtkWidget *partners = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    add_class(partners, "partners-section");
    gtk_box_append(GTK_BOX(partners), ui_label("CÁC HÃNG BAY NỔI BẬT", "partners-title"));
    GtkWidget *logos = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_box_append(GTK_BOX(logos), ui_partner_logo("vietnam_airline.png"));
    gtk_box_append(GTK_BOX(logos), ui_partner_logo("vietjet.png"));
    gtk_box_append(GTK_BOX(logos), ui_partner_logo("china_airline.png"));
    gtk_box_append(GTK_BOX(logos), ui_partner_logo("hongkong_airline.png"));
    gtk_box_append(GTK_BOX(partners), logos);
    gtk_box_append(GTK_BOX(page), partners);
    app->welcome = ui_label("Chào mừng bạn đến với LTM Flight", "muted");
    gtk_widget_set_margin_start(app->welcome, 48);
    gtk_widget_set_margin_bottom(app->welcome, 24);
    gtk_box_append(GTK_BOX(page), app->welcome);
    return scroll;
}

static GtkWidget *ui_auth(App *app, gboolean signup) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(page, "auth-page");
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    add_class(card, "auth-card");
    gtk_widget_set_size_request(card, 440, -1);
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(card, 32);
    gtk_widget_set_margin_bottom(card, 32);
    gtk_box_append(GTK_BOX(page), card);
    gtk_widget_set_vexpand(card, TRUE);
    gtk_box_append(GTK_BOX(card), ui_label(signup ? "Tạo tài khoản" : "Đăng nhập", "auth-title"));
    gtk_box_append(GTK_BOX(card), ui_label(signup
        ? "Bắt đầu khám phá những chuyến bay cùng LTM Flight."
        : "Chào mừng trở lại! Đăng nhập để tiếp tục.", "muted"));
    if (signup) {
        ui_field(card, "TÊN", "Tên của bạn", &app->reg_first, FALSE);
        ui_field(card, "HỌ", "Họ của bạn", &app->reg_last, FALSE);
        ui_field(card, "EMAIL", "you@example.com", &app->reg_email, FALSE);
        ui_field(card, "MẬT KHẨU", "Nhập mật khẩu", &app->reg_password, TRUE);
    } else {
        ui_field(card, "EMAIL", "you@example.com", &app->login_email, FALSE);
        ui_field(card, "MẬT KHẨU", "Nhập mật khẩu", &app->login_password, TRUE);
    }
    GtkWidget *submit = ui_button(signup ? "Đăng ký" : "Đăng nhập", "primary-button",
                                    signup ? G_CALLBACK(register_clicked) : G_CALLBACK(login_clicked), app);
    gtk_widget_set_margin_top(submit, 10);
    gtk_box_append(GTK_BOX(card), submit);
    return page;
}

static GtkWidget *ui_content_page(const char *eyebrow, const char *title,
                                  const char *description, GtkWidget **body) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(page, "section");
    gtk_box_append(GTK_BOX(page), ui_label(eyebrow, "section-kicker"));
    gtk_box_append(GTK_BOX(page), ui_label(title, "section-title"));
    gtk_box_append(GTK_BOX(page), ui_label(description, "muted"));
    *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(*body, "content-card");
    gtk_widget_set_vexpand(*body, TRUE);
    gtk_widget_set_margin_top(*body, 14);
    gtk_box_append(GTK_BOX(page), *body);
    return page;
}

static void activate(GtkApplication *gtk_app, gpointer data) {
    App *app = data;
    if (app->window) {
        gtk_window_present(GTK_WINDOW(app->window));
        return;
    }
    if (app->hero_timer) {
        g_source_remove(app->hero_timer);
        app->hero_timer = 0;
    }
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css, "/vn/ltm/flight/style.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    app->window = gtk_application_window_new(gtk_app);
    g_object_add_weak_pointer(G_OBJECT(app->window), (gpointer *)&app->window);
    gtk_window_set_title(GTK_WINDOW(app->window), "LTM Flight · Đặt vé máy bay");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1180, 800);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(root, "app-shell");
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    add_class(header, "topbar");

    GtkWidget *logo = gtk_picture_new_for_resource("/vn/ltm/flight/logo.png");
    gtk_picture_set_content_fit(GTK_PICTURE(logo), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_size_request(logo, 160, 50);
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(header), logo);

    app->nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 28);
    gtk_widget_set_hexpand(app->nav, TRUE);
    gtk_widget_set_halign(app->nav, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app->nav, GTK_ALIGN_CENTER);
    ui_nav_add(app->nav, app, "Trang chủ", "home", FALSE);
    ui_nav_add(app->nav, app, "Chuyến bay", "flights", FALSE);
    ui_nav_add(app->nav, app, "Vé của tôi", "tickets", FALSE);
    gtk_box_append(GTK_BOX(header), app->nav);

    GtkWidget *account = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_valign(account, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(account, GTK_ALIGN_END);

    GtkWidget *search_btn = ui_button("⌕", "header-search",
                                      G_CALLBACK(ui_header_search_clicked), app);
    gtk_widget_set_valign(search_btn, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(account), search_btn);

    GtkWidget *login_btn = ui_button("Đăng nhập", "header-btn-login",
                                     G_CALLBACK(ui_nav_clicked), app);
    g_object_set_data(G_OBJECT(login_btn), "page", (gpointer)"login");
    gtk_widget_set_valign(login_btn, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(account), login_btn);

    GtkWidget *signup_btn = ui_button("Đăng ký", "header-btn-signup",
                                      G_CALLBACK(ui_nav_clicked), app);
    g_object_set_data(G_OBJECT(signup_btn), "page", (gpointer)"signup");
    gtk_widget_set_valign(signup_btn, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(account), signup_btn);

    gtk_box_append(GTK_BOX(header), account);
    gtk_box_append(GTK_BOX(root), header);

    app->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app->stack),
        ui_animations_enabled() ? GTK_STACK_TRANSITION_TYPE_CROSSFADE
                                : GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_set_transition_duration(GTK_STACK(app->stack), 280);
    gtk_widget_set_vexpand(app->stack, TRUE);
    gtk_stack_add_named(GTK_STACK(app->stack), ui_home(app), "home");
    gtk_stack_add_named(GTK_STACK(app->stack), ui_auth(app, FALSE), "login");
    gtk_stack_add_named(GTK_STACK(app->stack), ui_auth(app, TRUE), "signup");

    GtkWidget *body = NULL;
    GtkWidget *flights = ui_content_page("DANH SÁCH CHUYẾN BAY", "Chọn hành trình của bạn",
        "Chọn một chuyến bay để xem thông tin chi tiết.", &body);
    app->results = gtk_list_box_new();
    add_class(app->results, "results-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->results), GTK_SELECTION_SINGLE);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(app->results),
        ui_label("Chưa có chuyến bay. Hãy tìm hành trình ở trang chủ.", "muted"));
    g_signal_connect(app->results, "row-selected", G_CALLBACK(selected_flight), app);
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), app->results);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(body), scroller);
    gtk_stack_add_named(GTK_STACK(app->stack), flights, "flights");

    GtkWidget *detail = ui_content_page("CHI TIẾT CHUYẾN BAY", "Thông tin hành trình",
        "Xem điểm đi, điểm đến và trạng thái chuyến bay.", &body);
    app->detail = ui_label("Chọn một chuyến bay để xem chi tiết.", "flight-route");
    gtk_box_append(GTK_BOX(body), app->detail);
    gtk_stack_add_named(GTK_STACK(app->stack), detail, "detail");

    GtkWidget *tickets = ui_content_page("TÀI KHOẢN CỦA BẠN", "Vé của tôi",
        "Thông tin đặt vé sẽ xuất hiện tại đây.", &body);
    gtk_box_append(GTK_BOX(body), ui_label(
        "Tính năng tra cứu vé sẽ có khi Go bridge hỗ trợ nghiệp vụ đặt vé.", "muted"));
    gtk_stack_add_named(GTK_STACK(app->stack), tickets, "tickets");
    show_page(app, "home");
    gtk_box_append(GTK_BOX(root), app->stack);
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    add_class(status_box, "status-bar");
    app->status = ui_label("Chưa kiểm tra kết nối server.", "muted");
    gtk_widget_set_hexpand(app->status, TRUE);
    gtk_box_append(GTK_BOX(status_box), app->status);
    gtk_box_append(GTK_BOX(status_box), ui_button("Kiểm tra kết nối", "secondary-button",
                                                G_CALLBACK(health_clicked), app));
    gtk_box_append(GTK_BOX(root), status_box);
    gtk_window_set_child(GTK_WINDOW(app->window), root);
    gtk_window_present(GTK_WINDOW(app->window));
#ifndef HAVE_GST
    if (ui_animations_enabled())
        app->hero_timer = g_timeout_add_seconds(8, ui_hero_tick, app);
#endif
}

int main(int argc, char **argv) {
#ifdef HAVE_GST
    gst_init(&argc, &argv);
#endif
    const char *host = g_getenv("LTM_SERVER_HOST");
    const char *port_text = g_getenv("LTM_SERVER_PORT");
    if (!host || !*host) host = "127.0.0.1";
    char *end = NULL;
    unsigned long parsed = port_text ? strtoul(port_text, &end, 10) : 9000;
    if (parsed == 0 || parsed > 65535 || (port_text && (!end || *end))) {
        g_printerr("LTM_SERVER_PORT phải là số từ 1 đến 65535.\n"); return 1;
    }
    App app = {0};
    app.network = net_client_new(host, (uint16_t)parsed);
    if (!app.network) { g_printerr("Không khởi tạo được Winsock/client TCP.\n"); return 1; }
    app.queue = g_async_queue_new();
    app.worker = g_thread_new("tcp-client", network_worker, &app);
    GtkApplication *gtk_app = gtk_application_new("vn.ltm.flight", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);
    int rc = g_application_run(G_APPLICATION(gtk_app), argc, argv);
#ifdef HAVE_GST
    video_stop(&app);
    gst_deinit();
#endif
    if (app.hero_timer) g_source_remove(app.hero_timer);
    Task *stop = g_new0(Task, 1);
    g_async_queue_push(app.queue, stop);
    g_thread_join(app.worker);
    g_async_queue_unref(app.queue);
    net_client_free(app.network);
    g_object_unref(gtk_app);
    return rc;
}
