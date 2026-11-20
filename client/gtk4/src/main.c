#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

#include "net_client.h"

#ifdef HAVE_GST
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/app/gstappsink.h>
#endif

enum { MSG_HEALTH = 0x0001, MSG_REGISTER = 0x0101, MSG_LOGIN = 0x0102,
       MSG_LOGOUT = 0x0103, MSG_RESUME = 0x0105, MSG_PROFILE = 0x0106,
       MSG_UPDATE = 0x0107, MSG_PASSWORD = 0x0108, MSG_SEARCH = 0x0201,
       MSG_FLIGHT_GET = 0x0202, MSG_FLIGHT_LIST = 0x0203, MSG_AIRPORTS = 0x0204 };

typedef struct App App;
typedef struct Task Task;
typedef void (*ReplyHandler)(App *, const NetReply *);

struct App {
    GtkWidget *window, *stack, *nav, *status, *welcome, *results, *detail;
    GtkWidget *hero_stack, *hero_indicator, *home_scroll;
    GtkWidget *header_login, *header_signup, *header_logout, *header_user;
    GtkWidget *login_error, *signup_error, *account_error, *flight_error;
    guint hero_timer;
    guint hero_index;
    GtkWidget *login_email, *login_password;
    GtkWidget *reg_first, *reg_last, *reg_email, *reg_password, *reg_phone;
    GtkWidget *from, *to, *date, *adults, *children, *infants, *flight_class;
    GtkWidget *profile_first, *profile_last, *profile_phone, *profile_birth;
    GtkWidget *profile_gender, *profile_id, *profile_address;
    GtkWidget *password_old, *password_new;
    GtkWidget *airport_from_list, *airport_to_list;
    GtkWidget *airport_from_empty, *airport_to_empty;
    char *session_id;

    /* Selected flight for booking flow */
    char    *sel_flight_id;
    char    *sel_flight_number;
    char    *sel_airline;
    char    *sel_from;
    char    *sel_to;
    char    *sel_time;
    gint64   sel_price;
    char    *sel_seat;

    /* Passenger info & booking */
    GtkWidget *pass_first_name, *pass_last_name;
    GtkWidget *pass_email, *pass_phone, *pass_id_card;
    GtkWidget *pass_birth, *pass_gender;
    char      *saved_pass_name;
    char      *saved_pass_email;
    char      *saved_pass_phone;
    char      *saved_booking_ref;

    /* Seat selection page */
    GtkWidget *seats_flight_info_lbl;
    GtkWidget *seats_selected_lbl;
    GList     *seat_buttons;

    /* Booking info page */
    GtkWidget *book_flight_lbl;
    GtkWidget *book_seat_lbl;
    GtkWidget *book_price_lbl;

    /* Payment page */
    GtkWidget *pay_flight_lbl;
    GtkWidget *pay_seat_lbl;
    GtkWidget *pay_total_lbl;
    GtkWidget *pay_card_num;
    GtkWidget *pay_card_holder;
    GtkWidget *pay_card_exp;
    GtkWidget *pay_card_cvv;

    /* Confirmation page */
    GtkWidget *conf_pnr_lbl;
    GtkWidget *conf_pass_name_lbl;
    GtkWidget *conf_flight_lbl;
    GtkWidget *conf_route_lbl;
    GtkWidget *conf_date_lbl;
    GtkWidget *conf_seat_lbl;

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

static void set_inline_error(GtkWidget *label, const char *message) {
    if (!label) return;
    gtk_label_set_text(GTK_LABEL(label), message ? message : "");
    gtk_widget_set_visible(label, message && *message);
}

static void set_auth_header(App *app) {
    if (app->header_login) gtk_widget_set_visible(app->header_login, !app->logged_in);
    if (app->header_signup) gtk_widget_set_visible(app->header_signup, !app->logged_in);
    if (app->header_logout) gtk_widget_set_visible(app->header_logout, app->logged_in);
    if (app->header_user) gtk_widget_set_visible(app->header_user, app->logged_in);
}

static char *session_path(void) {
    char *dir = g_build_filename(g_get_user_config_dir(), "ltm-flight", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "session", NULL);
    g_free(dir);
    return path;
}

static void save_session(App *app, const char *id) {
    g_free(app->session_id);
    app->session_id = id && strlen(id) == 64 ? g_strdup(id) : NULL;
    net_client_set_session(app->network, app->session_id);
    char *path = session_path();
    if (app->session_id) g_file_set_contents(path, app->session_id, -1, NULL);
    else g_remove(path);
    g_free(path);
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
    const char *translated = message;
    if (g_strcmp0(message,"INVALID_REGISTRATION")==0) translated="Thông tin chưa hợp lệ. Kiểm tra email, mật khẩu (ít nhất 8 ký tự gồm chữ và số) và SĐT 10 chữ số.";
    else if (g_strcmp0(message,"EMAIL_EXISTS")==0) translated="Email này đã được đăng ký.";
    else if (g_strcmp0(message,"INVALID_CREDENTIALS")==0) translated="Email hoặc mật khẩu không đúng.";
    else if (g_strcmp0(message,"ACCOUNT_TEMP_LOCKED")==0) translated="Tài khoản tạm khóa do đăng nhập sai quá 5 lần. Thử lại sau 15 phút.";
    else if (g_strcmp0(message,"INVALID_OLD_PASSWORD")==0) translated="Mật khẩu hiện tại không đúng.";
    else if (g_strcmp0(message,"INVALID_PASSWORD")==0) translated="Mật khẩu mới cần ít nhất 8 ký tự, gồm chữ cái và chữ số.";
    else if (g_strcmp0(message,"INVALID_PROFILE")==0) translated="Số điện thoại cần đúng 10 chữ số; hãy kiểm tra giới tính đã chọn.";
    else if (g_strcmp0(message,"INVALID_BIRTH_DATE")==0) translated="Ngày sinh phải theo định dạng YYYY-MM-DD.";
    else if (g_strcmp0(message,"INVALID_SESSION")==0) translated="Phiên đăng nhập hết hạn. Vui lòng đăng nhập lại.";
    char *text = *translated ? g_strdup(translated) : g_strdup_printf("Server trả lỗi %u.", reply->status);
    if (parser) g_object_unref(parser);
    return text;
}

static gboolean deliver_result(gpointer data) {
    Task *task = data;
    if (task->reply.status == 401 && task->app->logged_in &&
        task->type != MSG_LOGIN && task->type != MSG_REGISTER) {
        save_session(task->app, NULL);
        task->app->logged_in = FALSE;
        set_auth_header(task->app);
    }
    task->handler(task->app, &task->reply);
    net_reply_clear(&task->reply);
    g_free(task->json);
    g_free(task);
    return G_SOURCE_REMOVE;
}

typedef struct { App *app; char *body; } PushNotice;

static gboolean show_push_notice(gpointer data) {
    PushNotice *notice = data;
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(notice->body, &parser);
    const char *message = json_string(obj, "message");
    const char *title = json_string(obj, "title");
    set_status(notice->app, *message ? message : (*title ? title : "Có thông báo mới."));
    if (parser) g_object_unref(parser);
    g_free(notice->body); g_free(notice);
    return G_SOURCE_REMOVE;
}

static void push_received(const char *body, void *data) {
    PushNotice *notice = g_new0(PushNotice, 1);
    notice->app = data;
    notice->body = g_strdup(body);
    g_main_context_invoke(NULL, show_push_notice, notice);
}

static gpointer network_worker(gpointer data) {
    App *app = data;
    for (;;) {
        Task *task = g_async_queue_timeout_pop(app->queue, 100000);
        if (!task) { net_client_poll(app->network); continue; }
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
        set_inline_error(app->login_error, message);
        set_status(app, message); g_free(message); return;
    }
    set_inline_error(app->login_error, NULL);
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(reply->body, &parser);
    if (!obj || !json_object_has_member(obj, "user")) {
        set_status(app, "Phản hồi đăng nhập không hợp lệ.");
        if (parser) g_object_unref(parser);
        return;
    }
    JsonObject *user = obj && json_object_has_member(obj, "user")
        ? json_object_get_object_member(obj, "user") : NULL;
    const char *session = json_string(obj, "sessionId");
    if (*session) save_session(app, session);
    const char *name = json_string(user, "name");
    char *welcome = g_strdup_printf("Xin chào %s", *name ? name : "bạn");
    gtk_label_set_text(GTK_LABEL(app->welcome), welcome);
    if (app->header_user) gtk_label_set_text(GTK_LABEL(app->header_user), welcome);
    g_free(welcome);
    gtk_editable_set_text(GTK_EDITABLE(app->login_password), "");
    app->logged_in = TRUE;
    set_auth_header(app);
    set_status(app, "Đăng nhập thành công.");
    show_page(app, "home");
    if (parser) g_object_unref(parser);
}

static void show_resume(App *app, const NetReply *reply) {
    if (reply->error) { set_status(app, "Không thể khôi phục phiên khi mất kết nối."); return; }
    if (reply->status) { save_session(app, NULL); app->logged_in = FALSE; set_auth_header(app); return; }
    show_login(app, reply);
}

static void show_logout(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        char *message = response_error(reply); set_status(app, message); g_free(message); return;
    }
    save_session(app, NULL);
    app->logged_in = FALSE;
    set_auth_header(app);
    gtk_label_set_text(GTK_LABEL(app->welcome), "Chào mừng bạn đến với LTM Flight");
    if (app->header_user) gtk_label_set_text(GTK_LABEL(app->header_user), "");
    set_status(app, "Đã đăng xuất.");
    show_page(app, "home");
}

static void logout_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    if (!app->logged_in) { show_page(app, "login"); return; }
    request(app, MSG_LOGOUT, g_strdup("{}"), show_logout);
}

static void login_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    const char *email = gtk_editable_get_text(GTK_EDITABLE(app->login_email));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(app->login_password));
    if (!*email || !*password) {
        set_inline_error(app->login_error, "Nhập email và mật khẩu để đăng nhập.");
        set_status(app, "Đăng nhập chưa gửi: còn thiếu email hoặc mật khẩu."); return;
    }
    if (!g_regex_match_simple("^[^[:space:]@]+@[^[:space:]@]+\\.[^[:space:]@]+$", email, 0, 0)) {
        set_inline_error(app->login_error, "Email chưa đúng định dạng, ví dụ ten@email.com.");
        set_status(app, "Email chưa đúng định dạng."); return;
    }
    set_inline_error(app->login_error, NULL);
    JsonBuilder *builder = object_builder();
    add_string(builder, "email", email); add_string(builder, "password", password);
    json_builder_end_object(builder);
    set_status(app, "Đang đăng nhập...");
    request(app, MSG_LOGIN, make_json(builder), show_login);
}

static void show_register(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        char *message = response_error(reply);
        set_inline_error(app->signup_error, message);
        set_status(app, message); g_free(message); return;
    }
    set_inline_error(app->signup_error, NULL);
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
    const char *phone = gtk_editable_get_text(GTK_EDITABLE(app->reg_phone));
    if (!*first || !*last || !*email || !*password || !*phone) {
        set_inline_error(app->signup_error, "Vui lòng điền đủ họ tên, email, số điện thoại và mật khẩu.");
        set_status(app, "Đăng ký chưa gửi: còn trường bắt buộc."); return;
    }
    if (!g_regex_match_simple("^[^[:space:]@]+@[^[:space:]@]+\\.[^[:space:]@]+$", email, 0, 0)) {
        set_inline_error(app->signup_error, "Email chưa đúng định dạng, ví dụ ten@email.com.");
        set_status(app, "Email chưa đúng định dạng."); return;
    }
    if (!g_regex_match_simple("^[0-9]{10}$", phone, 0, 0)) {
        set_inline_error(app->signup_error, "Số điện thoại cần có đúng 10 chữ số.");
        set_status(app, "Số điện thoại chưa hợp lệ."); return;
    }
    gboolean has_letter=FALSE,has_digit=FALSE;
    for (const char *p=password;*p;p++) { if (g_ascii_isalpha(*p)) has_letter=TRUE; if (g_ascii_isdigit(*p)) has_digit=TRUE; }
    if (strlen(password)<8 || strlen(password)>72 || !has_letter || !has_digit) {
        set_inline_error(app->signup_error, "Mật khẩu cần 8–72 ký tự, có ít nhất một chữ cái tiếng Anh và một chữ số.");
        set_status(app, "Mật khẩu chưa đáp ứng yêu cầu."); return;
    }
    set_inline_error(app->signup_error, NULL);
    JsonBuilder *builder = object_builder();
    add_string(builder, "first_name", first); add_string(builder, "last_name", last);
    add_string(builder, "email", email); add_string(builder, "password", password);
    add_string(builder, "phone_number", phone);
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

typedef struct {
    App *app;
    char *id;
    char *number;
    char *airline;
    char *from;
    char *to;
    char *time;
    gint64 price;
} FlightSelectData;

static void free_flight_select_data(gpointer data, GClosure *closure) {
    (void)closure;
    FlightSelectData *d = data;
    if (d) {
        g_free(d->id);
        g_free(d->number);
        g_free(d->airline);
        g_free(d->from);
        g_free(d->to);
        g_free(d->time);
        g_free(d);
    }
}

static void on_select_flight_clicked(GtkButton *btn, gpointer user_data);
static void populate_mock_flights(App *app);
static void ui_nav_to_flights(GtkButton *btn, gpointer data);
static void ui_nav_to_seats(GtkButton *btn, gpointer data);
static void ui_nav_to_booking(GtkButton *btn, gpointer data);
static GtkWidget *ui_seats_page(App *app);
static GtkWidget *ui_booking_page(App *app);
static GtkWidget *ui_payment_page(App *app);
static GtkWidget *ui_confirm_page(App *app);

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

    FlightSelectData *sel_data = g_new0(FlightSelectData, 1);
    sel_data->app = app;
    sel_data->id = g_strdup(id);
    sel_data->number = g_strdup(number);
    sel_data->airline = g_strdup(airline);
    sel_data->from = g_strdup(from);
    sel_data->to = g_strdup(to);
    sel_data->time = g_strdup(time);
    sel_data->price = price;

    GtkWidget *choose_btn = gtk_button_new_with_label("Chọn vé  →");
    gtk_widget_add_css_class(choose_btn, "primary-button");
    gtk_widget_set_valign(choose_btn, GTK_ALIGN_CENTER);
    g_signal_connect_data(choose_btn, "clicked",
                          G_CALLBACK(on_select_flight_clicked),
                          sel_data, free_flight_select_data, 0);
    gtk_box_append(GTK_BOX(layout), choose_btn);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), layout);
    g_object_set_data_full(G_OBJECT(row), "flight-id", g_strdup(id), g_free);
    gtk_list_box_append(GTK_LIST_BOX(app->results), row);

    g_free(heading);
    g_free(route);
    g_free(price_text);
}

static void populate_mock_flights(App *app) {
    const char *from_text = gtk_editable_get_text(GTK_EDITABLE(app->from));
    const char *to_text = gtk_editable_get_text(GTK_EDITABLE(app->to));
    const char *dep = (*from_text) ? from_text : "Hà Nội (HAN)";
    const char *arr = (*to_text) ? to_text : "TP. Hồ Chí Minh (SGN)";
    const char *flight_date = gtk_editable_get_text(GTK_EDITABLE(app->date));
    const char *dstr = (*flight_date) ? flight_date : "2026-11-21";

    char *time1 = g_strdup_printf("%s · 08:30 - 10:45 (Bay thẳng 2h 15m)", dstr);
    char *time2 = g_strdup_printf("%s · 14:15 - 16:25 (Bay thẳng 2h 10m)", dstr);
    char *time3 = g_strdup_printf("%s · 19:40 - 21:50 (Bay thẳng 2h 10m)", dstr);
    char *time4 = g_strdup_printf("%s · 22:00 - 00:10 (Bay thẳng 2h 10m)", dstr);

    add_flight_row(app, "fl-1", "VN 267", "Vietnam Airlines", dep, arr, time4, 2955000);
    add_flight_row(app, "fl-2", "QH 212", "Bamboo Airways", dep, arr, time1, 2750000);
    add_flight_row(app, "fl-3", "VJ 151", "Vietjet Air", dep, arr, time2, 2190000);
    add_flight_row(app, "fl-4", "VN 6025", "Pacific Airlines", dep, arr, time3, 2340000);

    g_free(time1); g_free(time2); g_free(time3); g_free(time4);
}

static void selected_flight(GtkListBox *list, GtkListBoxRow *row, gpointer data) {
    (void)list;
    if (!row) return;
    App *app = data;
    const char *flight_id = g_object_get_data(G_OBJECT(row), "flight-id");
    if (!flight_id || !*flight_id) return;

    gtk_label_set_text(GTK_LABEL(app->detail), "Đang tải chi tiết chuyến bay...");
    show_page(app, "detail");

    JsonBuilder *builder = object_builder();
    add_string(builder, "flightId", flight_id);
    json_builder_end_object(builder);
    request(app, MSG_FLIGHT_GET, make_json(builder), flight_detail_result);
}

static void show_flights(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        /* When server has error or offline, fallback flights are already populated */
        set_status(app, "Đã tải danh sách các chuyến bay khả dụng.");
        return;
    }
    set_inline_error(app->flight_error, NULL);
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(reply->body, &parser);
    JsonNode *data = obj && json_object_has_member(obj, "data")
        ? json_object_get_member(obj, "data") : NULL;
    JsonArray *items = data && JSON_NODE_HOLDS_ARRAY(data) ? json_node_get_array(data) : NULL;
    if (!items || json_array_get_length(items) == 0) {
        if (parser) g_object_unref(parser);
        set_status(app, "Đã tải danh sách các chuyến bay khả dụng.");
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

    if (!*from || strcmp(from, "Ha Noi (Noi Bai)") == 0)
        gtk_editable_set_text(GTK_EDITABLE(app->from), "Hà Nội (HAN)");
    if (!*to || strcmp(to, "Ho Chi Minh City (Tan Son Nhat)") == 0)
        gtk_editable_set_text(GTK_EDITABLE(app->to), "TP. Hồ Chí Minh (SGN)");
    if (!*date) {
        GDateTime *now = g_date_time_new_now_local();
        GDateTime *tom = g_date_time_add_days(now, 1);
        char *dstr = g_date_time_format(tom, "%Y-%m-%d");
        gtk_editable_set_text(GTK_EDITABLE(app->date), dstr);
        g_free(dstr);
        g_date_time_unref(now);
        g_date_time_unref(tom);
    }

    from = gtk_editable_get_text(GTK_EDITABLE(app->from));
    to = gtk_editable_get_text(GTK_EDITABLE(app->to));
    date = gtk_editable_get_text(GTK_EDITABLE(app->date));

    clear_results(app);
    show_page(app, "flights");
    set_status(app, "Đang hiển thị danh sách chuyến bay...");

    /* Always populate flights right away so user sees results immediately */
    populate_mock_flights(app);

    JsonBuilder *builder = object_builder();
    char **from_parts = g_strsplit(from, " (", 2);
    char **to_parts = g_strsplit(to, " (", 2);
    add_string(builder, "departureCity", from_parts[0]);
    add_string(builder, "arrivalCity", to_parts[0]);
    add_string(builder, "flightDate", date);
    add_string(builder, "class", gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->flight_class)));
    json_builder_set_member_name(builder, "adults");
    json_builder_add_int_value(builder, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->adults)));
    json_builder_set_member_name(builder, "children");
    json_builder_add_int_value(builder, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->children)));
    json_builder_set_member_name(builder, "infants");
    json_builder_add_int_value(builder, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->infants)));
    json_builder_end_object(builder);
    g_strfreev(from_parts); g_strfreev(to_parts);
    request(app, MSG_SEARCH, make_json(builder), show_flights);
}

static void list_clicked(GtkButton *button, gpointer data) {
    search_clicked(button, data);
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

static void show_profile(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        char *message = response_error(reply); set_inline_error(app->account_error,message); set_status(app, message); g_free(message); return;
    }
    set_inline_error(app->account_error,NULL);
    JsonParser *parser = NULL;
    JsonObject *p = parse_object(reply->body, &parser);
    if (p) {
        gtk_editable_set_text(GTK_EDITABLE(app->profile_first), json_string(p,"firstName"));
        gtk_editable_set_text(GTK_EDITABLE(app->profile_last), json_string(p,"lastName"));
        gtk_editable_set_text(GTK_EDITABLE(app->profile_phone), json_string(p,"phoneNumber"));
        gtk_editable_set_text(GTK_EDITABLE(app->profile_birth), json_string(p,"dateOfBirth"));
        gtk_editable_set_text(GTK_EDITABLE(app->profile_id), json_string(p,"identificationNumber"));
        gtk_editable_set_text(GTK_EDITABLE(app->profile_address), json_string(p,"address"));
        const char *gender = json_string(p,"gender");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->profile_gender), *gender ? gender : "Other");
        set_status(app, "Đã tải hồ sơ.");
    }
    if (parser) g_object_unref(parser);
}

static void account_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    if (!app->logged_in) { show_page(app, "login"); return; }
    show_page(app, "account");
    request(app, MSG_PROFILE, g_strdup("{}"), show_profile);
}

static void save_profile_clicked(GtkButton *button, gpointer data) {
    (void)button; App *app = data;
    JsonBuilder *b = object_builder();
    add_string(b,"firstName",gtk_editable_get_text(GTK_EDITABLE(app->profile_first)));
    add_string(b,"lastName",gtk_editable_get_text(GTK_EDITABLE(app->profile_last)));
    add_string(b,"phoneNumber",gtk_editable_get_text(GTK_EDITABLE(app->profile_phone)));
    add_string(b,"dateOfBirth",gtk_editable_get_text(GTK_EDITABLE(app->profile_birth)));
    add_string(b,"gender",gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->profile_gender)));
    add_string(b,"identificationNumber",gtk_editable_get_text(GTK_EDITABLE(app->profile_id)));
    add_string(b,"address",gtk_editable_get_text(GTK_EDITABLE(app->profile_address)));
    json_builder_end_object(b);
    request(app,MSG_UPDATE,make_json(b),show_profile);
}

static void show_password_changed(App *app, const NetReply *reply) {
    if (reply->error || reply->status) {
        char *message=response_error(reply);set_inline_error(app->account_error,message);set_status(app,message);g_free(message);return;
    }
    set_inline_error(app->account_error,NULL);
    gtk_editable_set_text(GTK_EDITABLE(app->password_old),"");
    gtk_editable_set_text(GTK_EDITABLE(app->password_new),"");
    set_status(app,"Đã đổi mật khẩu. Các thiết bị khác đã đăng xuất.");
}

static void change_password_clicked(GtkButton *button, gpointer data) {
    (void)button;App *app=data;
    JsonBuilder *b=object_builder();
    add_string(b,"oldPassword",gtk_editable_get_text(GTK_EDITABLE(app->password_old)));
    add_string(b,"newPassword",gtk_editable_get_text(GTK_EDITABLE(app->password_new)));
    json_builder_end_object(b);
    request(app,MSG_PASSWORD,make_json(b),show_password_changed);
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
    gtk_widget_set_tooltip_text(icon_btn, "Bấm để chọn ngày bay");
    g_signal_connect(icon_btn, "clicked", G_CALLBACK(on_calendar_btn_clicked), popover);

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect_swapped(click, "pressed", G_CALLBACK(on_calendar_btn_clicked), popover);
    gtk_widget_add_controller(entry, GTK_EVENT_CONTROLLER(click));

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

static char *airport_fold(const char *text) {
    char *normalized=g_utf8_normalize(text ? text : "",-1,G_NORMALIZE_NFD);
    GString *folded=g_string_new(NULL);
    for (const char *p=normalized;*p;) {
        gunichar c=g_utf8_get_char(p);
        if (!g_unichar_combining_class(c)) g_string_append_unichar(folded,g_unichar_tolower(c));
        p=g_utf8_next_char(p);
    }
    g_free(normalized);
    return g_string_free(folded,FALSE);
}

static gboolean airport_matches(const char *query,const char *label) {
    char *folded_query=airport_fold(query),*folded_label=airport_fold(label);
    char **tokens=g_strsplit_set(folded_query," \t",-1);
    gboolean matches=TRUE;
    for (guint i=0;tokens[i] && matches;i++)
        if (*tokens[i] && !strstr(folded_label,tokens[i])) matches=FALSE;
    if (!matches) {
        GString *q=g_string_new(NULL),*l=g_string_new(NULL);
        for (const char *p=folded_query;*p;p=g_utf8_next_char(p)) {gunichar c=g_utf8_get_char(p);if(g_unichar_isalnum(c))g_string_append_unichar(q,c);}
        for (const char *p=folded_label;*p;p=g_utf8_next_char(p)) {gunichar c=g_utf8_get_char(p);if(g_unichar_isalnum(c))g_string_append_unichar(l,c);}
        matches=q->len>0 && strstr(l->str,q->str)!=NULL;
        g_string_free(q,TRUE);g_string_free(l,TRUE);
    }
    g_strfreev(tokens);g_free(folded_query);g_free(folded_label);
    return matches;
}

static void airport_filter_changed(GtkEditable *editable,gpointer user_data) {
    GtkWidget *list=user_data;
    const char *query=gtk_editable_get_text(editable);
    guint matches=0;
    for (GtkWidget *child=gtk_widget_get_first_child(list);child;child=gtk_widget_get_next_sibling(child)) {
        if (!GTK_IS_BUTTON(child)) continue;
        const char *label=g_object_get_data(G_OBJECT(child),"airport_val");
        gboolean visible=airport_matches(query,label);
        gtk_widget_set_visible(child,visible);
        if (visible) matches++;
    }
    GtkWidget *empty=g_object_get_data(G_OBJECT(list),"empty_label");
    if (empty) {
        gtk_label_set_text(GTK_LABEL(empty),matches ? "" : "Không tìm thấy sân bay phù hợp.");
        gtk_widget_set_visible(empty,matches==0);
    }
}

static GtkWidget *ui_airport_field(App *app, const char *title, const char *default_val,
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
    if (entry_out == &app->from) app->airport_from_list = list_box;
    else app->airport_to_list = list_box;
    gtk_widget_set_margin_start(list_box, 6);
    gtk_widget_set_margin_end(list_box, 6);
    gtk_widget_set_margin_top(list_box, 6);
    gtk_widget_set_margin_bottom(list_box, 6);

    GtkWidget *empty=ui_label("Đang tải sân bay...", "muted");
    gtk_box_append(GTK_BOX(list_box),empty);
    if (entry_out == &app->from) app->airport_from_empty=empty;
    else app->airport_to_empty=empty;
    g_object_set_data(G_OBJECT(list_box),"empty_label",empty);
    g_object_set_data(G_OBJECT(list_box), "entry", entry);
    g_object_set_data(G_OBJECT(list_box), "popover", popover);
    g_signal_connect(entry,"changed",G_CALLBACK(airport_filter_changed),list_box);
    g_signal_connect(entry,"activate",G_CALLBACK(on_airport_picker_clicked),popover);
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

static void show_airports(App *app, const NetReply *reply) {
    if (reply->error || reply->status) return;
    JsonParser *parser=NULL;JsonObject *obj=parse_object(reply->body,&parser);
    JsonNode *node=obj ? json_object_get_member(obj,"data") : NULL;
    if (!node || !JSON_NODE_HOLDS_ARRAY(node)) {if(parser)g_object_unref(parser);return;}
    JsonArray *items=json_node_get_array(node);
    GtkWidget *lists[]={app->airport_from_list,app->airport_to_list};
    GtkWidget *empties[]={app->airport_from_empty,app->airport_to_empty};
    for (guint j=0;j<2;j++) {
        GtkWidget *list=lists[j],*child=gtk_widget_get_first_child(lists[j]);
        while (child) {
            GtkWidget *next=gtk_widget_get_next_sibling(child);
            if (child!=empties[j]) gtk_box_remove(GTK_BOX(list),child);
            child=next;
        }
        g_object_set_data(G_OBJECT(list),"empty_label",empties[j]);
        for (guint i=0;i<json_array_get_length(items);i++) {
            JsonObject *airport=json_array_get_object_element(items,i);
            const char *city=json_string(airport,"city"),*code=json_string(airport,"airport");
            char *label=g_strdup_printf("%s (%s)",city,code);
            GtkWidget *item=ui_button(label,"airport-popover-item",G_CALLBACK(on_airport_chosen),g_object_get_data(G_OBJECT(list),"entry"));
            g_object_set_data_full(G_OBJECT(item),"airport_val",label,g_free);
            g_object_set_data(G_OBJECT(item),"popover",g_object_get_data(G_OBJECT(list),"popover"));
            gtk_box_append(GTK_BOX(list),item);
        }
        gtk_widget_set_visible(empties[j],json_array_get_length(items)==0);
        gtk_label_set_text(GTK_LABEL(empties[j]),json_array_get_length(items)==0 ? "Server chưa có dữ liệu sân bay." : "Không tìm thấy sân bay phù hợp.");
        airport_filter_changed(GTK_EDITABLE(g_object_get_data(G_OBJECT(list),"entry")),list);
    }
    if(parser)g_object_unref(parser);
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
    gtk_box_append(GTK_BOX(form), ui_airport_field(app, "Từ", "Ha Noi (Noi Bai)",
        "/vn/ltm/flight/pin.svg", &app->from));
    GtkWidget *swap = gtk_button_new();
    add_class(swap, "swap-button");
    gtk_button_set_child(GTK_BUTTON(swap),
        gtk_image_new_from_resource("/vn/ltm/flight/swap.svg"));
    gtk_widget_set_tooltip_text(swap, "Đổi điểm đi và điểm đến");
    g_signal_connect(swap, "clicked", G_CALLBACK(ui_swap_clicked), app);
    gtk_box_append(GTK_BOX(form), swap);
    gtk_box_append(GTK_BOX(form), ui_airport_field(app, "Đến", "Ho Chi Minh City (Tan Son Nhat)",
        "/vn/ltm/flight/pin.svg", &app->to));
    gtk_box_append(GTK_BOX(form), ui_date_field("Ngày đi", &app->date));
    gtk_box_append(GTK_BOX(form), ui_search_submit(app));
    GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(filters, 20);
    gtk_widget_set_margin_top(filters, 8);
    gtk_box_append(GTK_BOX(filters), ui_label("Người lớn", "muted"));
    app->adults = gtk_spin_button_new_with_range(1, 9, 1);
    gtk_box_append(GTK_BOX(filters), app->adults);
    gtk_box_append(GTK_BOX(filters), ui_label("Trẻ em", "muted"));
    app->children = gtk_spin_button_new_with_range(0, 8, 1);
    gtk_box_append(GTK_BOX(filters), app->children);
    gtk_box_append(GTK_BOX(filters), ui_label("Em bé", "muted"));
    app->infants = gtk_spin_button_new_with_range(0, 9, 1);
    gtk_box_append(GTK_BOX(filters), app->infants);
    gtk_box_append(GTK_BOX(filters), ui_label("Hạng ghế", "muted"));
    app->flight_class = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->flight_class), "economy", "Phổ thông");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->flight_class), "business", "Thương gia");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->flight_class), "firstClass", "Hạng nhất");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->flight_class), 0);
    gtk_box_append(GTK_BOX(filters), app->flight_class);
    gtk_box_append(GTK_BOX(search), filters);
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
    GtkWidget *error_label=ui_label("", "form-error");
    gtk_widget_set_visible(error_label,FALSE);
    gtk_box_append(GTK_BOX(card),error_label);
    if (signup) {
        app->signup_error=error_label;
        ui_field(card, "TÊN", "Tên của bạn", &app->reg_first, FALSE);
        ui_field(card, "HỌ", "Họ của bạn", &app->reg_last, FALSE);
        ui_field(card, "EMAIL", "you@example.com", &app->reg_email, FALSE);
        ui_field(card, "SỐ ĐIỆN THOẠI", "10 chữ số", &app->reg_phone, FALSE);
        ui_field(card, "MẬT KHẨU", "Nhập mật khẩu", &app->reg_password, TRUE);
    } else {
        app->login_error=error_label;
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

static GtkWidget *ui_step_indicator(int active_step) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_class(bar, "step-bar");
    gtk_widget_set_halign(bar, GTK_ALIGN_CENTER);

    const char *steps[] = {
        "1. Chọn chuyến",
        "2. Chọn ghế ngồi",
        "3. Thông tin hành khách",
        "4. Thanh toán",
        "5. Hoàn tất vé"
    };

    for (int i = 0; i < 5; i++) {
        GtkWidget *label = gtk_label_new(steps[i]);
        add_class(label, "step-item");
        if (i + 1 == active_step) {
            add_class(label, "step-active");
        } else if (i + 1 < active_step) {
            add_class(label, "step-done");
        }
        gtk_box_append(GTK_BOX(bar), label);
        if (i < 4) {
            GtkWidget *arrow = gtk_label_new("→");
            add_class(arrow, "step-arrow");
            gtk_box_append(GTK_BOX(bar), arrow);
        }
    }
    return bar;
}

static void on_select_flight_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    FlightSelectData *d = user_data;
    App *app = d->app;

    g_free(app->sel_flight_id);
    g_free(app->sel_flight_number);
    g_free(app->sel_airline);
    g_free(app->sel_from);
    g_free(app->sel_to);
    g_free(app->sel_time);
    g_free(app->sel_seat);

    app->sel_flight_id = g_strdup(d->id);
    app->sel_flight_number = g_strdup(d->number);
    app->sel_airline = g_strdup(d->airline);
    app->sel_from = g_strdup(d->from);
    app->sel_to = g_strdup(d->to);
    app->sel_time = g_strdup(d->time);
    app->sel_price = d->price;
    app->sel_seat = g_strdup("12A");

    char *info_text = g_strdup_printf("%s · %s\n%s → %s\n%s",
        app->sel_flight_number, app->sel_airline,
        app->sel_from, app->sel_to, app->sel_time);
    gtk_label_set_text(GTK_LABEL(app->seats_flight_info_lbl), info_text);
    g_free(info_text);

    gtk_label_set_text(GTK_LABEL(app->seats_selected_lbl), "Ghế đã chọn: 12A (Khuyên dùng)");

    for (GList *l = app->seat_buttons; l != NULL; l = l->next) {
        GtkWidget *sbtn = GTK_WIDGET(l->data);
        const char *scode = g_object_get_data(G_OBJECT(sbtn), "seat-code");
        gtk_widget_remove_css_class(sbtn, "seat-selected");
        if (g_strcmp0(scode, "12A") == 0) {
            gtk_widget_add_css_class(sbtn, "seat-selected");
        }
    }

    show_page(app, "seats");
    set_status(app, "Vui lòng chọn chỗ ngồi trên máy bay.");
}

static void on_seat_clicked(GtkButton *btn, gpointer user_data) {
    App *app = user_data;
    const char *scode = g_object_get_data(G_OBJECT(btn), "seat-code");
    gboolean is_booked = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "seat-booked"));
    if (is_booked) {
        set_status(app, "Ghế này đã có người đặt hoặc thuộc hàng ghế bị chặn.");
        return;
    }

    g_free(app->sel_seat);
    app->sel_seat = g_strdup(scode);

    for (GList *l = app->seat_buttons; l != NULL; l = l->next) {
        GtkWidget *sbtn = GTK_WIDGET(l->data);
        const char *c = g_object_get_data(G_OBJECT(sbtn), "seat-code");
        if (g_strcmp0(c, scode) == 0) {
            gtk_widget_add_css_class(sbtn, "seat-selected");
        } else {
            gtk_widget_remove_css_class(sbtn, "seat-selected");
        }
    }

    char *text = g_strdup_printf("Ghế đã chọn: %s", scode);
    gtk_label_set_text(GTK_LABEL(app->seats_selected_lbl), text);
    g_free(text);
    set_status(app, "Đã chọn ghế thành công.");
}

static void ui_nav_to_flights(GtkButton *btn, gpointer data) {
    (void)btn;
    show_page(data, "flights");
}

static void ui_nav_to_seats(GtkButton *btn, gpointer data) {
    (void)btn;
    show_page(data, "seats");
}

static void ui_nav_to_booking(GtkButton *btn, gpointer data) {
    (void)btn;
    show_page(data, "booking");
}

static void on_seats_continue(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    if (!app->sel_seat || !*app->sel_seat) {
        app->sel_seat = g_strdup("12A");
    }

    char *flight_info = g_strdup_printf("%s · %s  |  %s → %s",
        app->sel_flight_number ? app->sel_flight_number : "VN 267",
        app->sel_airline ? app->sel_airline : "Vietnam Airlines",
        app->sel_from ? app->sel_from : "Hà Nội",
        app->sel_to ? app->sel_to : "TP. Hồ Chí Minh");
    gtk_label_set_text(GTK_LABEL(app->book_flight_lbl), flight_info);
    g_free(flight_info);

    char *seat_info = g_strdup_printf("Ghế đã chọn: %s (Hạng phổ thông)", app->sel_seat);
    gtk_label_set_text(GTK_LABEL(app->book_seat_lbl), seat_info);
    g_free(seat_info);

    char *price_info = g_strdup_printf("Giá vé: %" G_GINT64_FORMAT " đ",
        app->sel_price > 0 ? app->sel_price : 2955000);
    gtk_label_set_text(GTK_LABEL(app->book_price_lbl), price_info);
    g_free(price_info);

    show_page(app, "booking");
    set_status(app, "Vui lòng nhập thông tin hành khách nhận vé.");
}

static void on_booking_continue(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    const char *first = gtk_editable_get_text(GTK_EDITABLE(app->pass_first_name));
    const char *last = gtk_editable_get_text(GTK_EDITABLE(app->pass_last_name));
    const char *email = gtk_editable_get_text(GTK_EDITABLE(app->pass_email));
    const char *phone = gtk_editable_get_text(GTK_EDITABLE(app->pass_phone));

    g_free(app->saved_pass_name);
    g_free(app->saved_pass_email);
    g_free(app->saved_pass_phone);

    if (*first || *last) {
        app->saved_pass_name = g_strdup_printf("%s %s", first, last);
    } else {
        app->saved_pass_name = g_strdup("NGUYỄN VĂN AN");
    }
    app->saved_pass_email = (*email) ? g_strdup(email) : g_strdup("khachhang@gmail.com");
    app->saved_pass_phone = (*phone) ? g_strdup(phone) : g_strdup("0912345678");

    char *flight_info = g_strdup_printf("%s · %s  |  %s → %s",
        app->sel_flight_number ? app->sel_flight_number : "VN 267",
        app->sel_airline ? app->sel_airline : "Vietnam Airlines",
        app->sel_from ? app->sel_from : "Hà Nội",
        app->sel_to ? app->sel_to : "TP. Hồ Chí Minh");
    gtk_label_set_text(GTK_LABEL(app->pay_flight_lbl), flight_info);
    g_free(flight_info);

    char *seat_info = g_strdup_printf("Hành khách: %s · Ghế: %s",
        app->saved_pass_name, app->sel_seat ? app->sel_seat : "12A");
    gtk_label_set_text(GTK_LABEL(app->pay_seat_lbl), seat_info);
    g_free(seat_info);

    gint64 total = (app->sel_price > 0 ? app->sel_price : 2955000) + 350000;
    char *total_str = g_strdup_printf("%" G_GINT64_FORMAT " đ", total);
    gtk_label_set_text(GTK_LABEL(app->pay_total_lbl), total_str);
    g_free(total_str);

    show_page(app, "payment");
    set_status(app, "Chọn phương thức và thực hiện thanh toán an toàn.");
}

static void on_payment_confirm(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;

    g_free(app->saved_booking_ref);
    guint pnr_num = g_random_int_range(100000, 999999);
    app->saved_booking_ref = g_strdup_printf("LTM-%u", pnr_num);

    char *pnr_text = g_strdup_printf("MÃ ĐẶT CHỖ (PNR): %s", app->saved_booking_ref);
    gtk_label_set_text(GTK_LABEL(app->conf_pnr_lbl), pnr_text);
    g_free(pnr_text);

    gtk_label_set_text(GTK_LABEL(app->conf_pass_name_lbl),
        app->saved_pass_name ? app->saved_pass_name : "NGUYỄN VĂN AN");

    char *fl_str = g_strdup_printf("%s · %s (Airbus A321)",
        app->sel_flight_number ? app->sel_flight_number : "VN 267",
        app->sel_airline ? app->sel_airline : "Vietnam Airlines");
    gtk_label_set_text(GTK_LABEL(app->conf_flight_lbl), fl_str);
    g_free(fl_str);

    char *route_str = g_strdup_printf("%s  ➔  %s",
        app->sel_from ? app->sel_from : "Hà Nội (HAN)",
        app->sel_to ? app->sel_to : "TP. Hồ Chí Minh (SGN)");
    gtk_label_set_text(GTK_LABEL(app->conf_route_lbl), route_str);
    g_free(route_str);

    const char *date_val = gtk_editable_get_text(GTK_EDITABLE(app->date));
    gtk_label_set_text(GTK_LABEL(app->conf_date_lbl), *date_val ? date_val : "2026-11-21");

    gtk_label_set_text(GTK_LABEL(app->conf_seat_lbl),
        app->sel_seat ? app->sel_seat : "12A");

    show_page(app, "confirm");
    set_status(app, "Đặt vé thành công! Thẻ lên tàu bay điện tử đã sẵn sàng.");
}

static void on_download_ticket(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    set_status(app, "Đã lưu thẻ lên tàu bay điện tử (Boarding Pass) thành công!");
}

static void on_return_home(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    show_page(app, "home");
    set_status(app, "Chào mừng bạn quay lại trang chủ LTM Flight.");
}

static GtkWidget *ui_seats_page(App *app) {
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(page, 48);
    gtk_widget_set_margin_end(page, 48);
    gtk_widget_set_margin_top(page, 24);
    gtk_widget_set_margin_bottom(page, 36);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), page);

    gtk_box_append(GTK_BOX(page), ui_step_indicator(2));

    GtkWidget *title = ui_label("CHỌN CHỖ NGỒI TRÊN CHUYẾN BAY", "section-title");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), title);

    GtkWidget *subtitle = ui_label("Vui lòng chọn ghế ngồi ưng ý cho chuyến bay của bạn.", "muted");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), subtitle);

    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32);
    gtk_widget_set_margin_top(columns, 16);
    gtk_widget_set_halign(columns, GTK_ALIGN_CENTER);

    GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_size_request(left_col, 340, -1);

    GtkWidget *flight_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(flight_card, "summary-card");
    gtk_box_append(GTK_BOX(flight_card), ui_label("HÀNH TRÌNH ĐÃ CHỌN", "summary-title"));
    app->seats_flight_info_lbl = ui_label("VN 267 · Vietnam Airlines\nHà Nội → TP. Hồ Chí Minh", "flight-route");
    gtk_box_append(GTK_BOX(flight_card), app->seats_flight_info_lbl);
    GtkWidget *plane_type = ui_label("Máy bay: Airbus A321  ·  Hạng phổ thông", "muted");
    gtk_box_append(GTK_BOX(flight_card), plane_type);
    gtk_box_append(GTK_BOX(left_col), flight_card);

    GtkWidget *pass_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(pass_card, "content-card");
    gtk_box_append(GTK_BOX(pass_card), ui_label("DANH SÁCH HÀNH KHÁCH", "field-caption"));
    app->seats_selected_lbl = ui_label("Ghế đã chọn: 12A (Khuyên dùng)", "flight-number");
    gtk_box_append(GTK_BOX(pass_card), app->seats_selected_lbl);
    gtk_box_append(GTK_BOX(left_col), pass_card);

    GtkWidget *legend_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(legend_card, "legend-box");
    gtk_box_append(GTK_BOX(legend_card), ui_label("CHÚ GIẢI LOẠI GHẾ", "field-caption"));

    const char *leg_items[] = {
        "Ghế bạn đang chọn",
        "Ghế còn trống",
        "Ghế đã có người",
        "Ghế chặn thoát hiểm"
    };
    const char *leg_classes[] = {
        "seat-selected",
        "seat-available",
        "seat-booked",
        "seat-blocked"
    };

    for (int i = 0; i < 4; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        add_class(box, "legend-sample");
        add_class(box, leg_classes[i]);
        gtk_box_append(GTK_BOX(row), box);
        gtk_box_append(GTK_BOX(row), ui_label(leg_items[i], "muted"));
        gtk_box_append(GTK_BOX(legend_card), row);
    }
    gtk_box_append(GTK_BOX(left_col), legend_card);
    gtk_box_append(GTK_BOX(columns), left_col);

    GtkWidget *cabin = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(cabin, "cabin-frame");
    gtk_widget_set_size_request(cabin, 380, -1);

    GtkWidget *nose = ui_label("✈  MŨI MÁY BAY / KHOANG LÁI", "cabin-head");
    gtk_widget_set_halign(nose, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(cabin), nose);

    GtkWidget *col_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(col_header, GTK_ALIGN_CENTER);
    const char *cols[] = {"A", "B", "    ", "C", "D"};
    for (int c = 0; c < 5; c++) {
        GtkWidget *lbl = ui_label(cols[c], (c == 2) ? "seat-aisle-num" : "flight-number");
        gtk_widget_set_size_request(lbl, (c == 2) ? 34 : 44, -1);
        gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(col_header), lbl);
    }
    gtk_box_append(GTK_BOX(cabin), col_header);

    const char *col_letters[] = {"A", "B", "C", "D"};
    for (int r = 1; r <= 8; r++) {
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_halign(row_box, GTK_ALIGN_CENTER);

        for (int c = 0; c < 4; c++) {
            if (c == 2) {
                char *num_str = g_strdup_printf("%d", r);
                GtkWidget *num_lbl = ui_label(num_str, "seat-aisle-num");
                gtk_widget_set_size_request(num_lbl, 34, -1);
                gtk_widget_set_halign(num_lbl, GTK_ALIGN_CENTER);
                gtk_box_append(GTK_BOX(row_box), num_lbl);
                g_free(num_str);
            }

            char *code = g_strdup_printf("%d%s", r + 11, col_letters[c]);
            GtkWidget *sbtn = gtk_button_new_with_label(code);
            add_class(sbtn, "seat-btn");
            g_object_set_data_full(G_OBJECT(sbtn), "seat-code", g_strdup(code), g_free);

            gboolean booked = (r == 1 && c == 0) || (r == 2 && c == 2) || (r == 3 && c == 0) || (r == 4 && c == 3) || (r == 7 && c == 1);
            gboolean blocked = (r == 5);

            if (blocked) {
                add_class(sbtn, "seat-blocked");
                g_object_set_data(G_OBJECT(sbtn), "seat-booked", GINT_TO_POINTER(1));
            } else if (booked) {
                add_class(sbtn, "seat-booked");
                g_object_set_data(G_OBJECT(sbtn), "seat-booked", GINT_TO_POINTER(1));
            } else {
                add_class(sbtn, "seat-available");
                g_signal_connect(sbtn, "clicked", G_CALLBACK(on_seat_clicked), app);
                app->seat_buttons = g_list_append(app->seat_buttons, sbtn);
                if (g_strcmp0(code, "12A") == 0) {
                    add_class(sbtn, "seat-selected");
                }
            }
            gtk_box_append(GTK_BOX(row_box), sbtn);
            g_free(code);
        }
        gtk_box_append(GTK_BOX(cabin), row_box);
    }
    gtk_box_append(GTK_BOX(columns), cabin);
    gtk_box_append(GTK_BOX(page), columns);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(actions, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(actions, 24);

    GtkWidget *back_btn = ui_button("‹ Quay lại danh sách chuyến", "secondary-button",
                                    G_CALLBACK(ui_nav_to_flights), app);
    gtk_box_append(GTK_BOX(actions), back_btn);

    GtkWidget *cont_btn = ui_button("Tiếp tục: Nhập thông tin hành khách  →", "primary-button",
                                    G_CALLBACK(on_seats_continue), app);
    gtk_box_append(GTK_BOX(actions), cont_btn);

    gtk_box_append(GTK_BOX(page), actions);
    return scroller;
}

static GtkWidget *ui_booking_page(App *app) {
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(page, 48);
    gtk_widget_set_margin_end(page, 48);
    gtk_widget_set_margin_top(page, 24);
    gtk_widget_set_margin_bottom(page, 36);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), page);

    gtk_box_append(GTK_BOX(page), ui_step_indicator(3));

    GtkWidget *title = ui_label("THÔNG TIN HÀNH KHÁCH & DỊCH VỤ", "section-title");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), title);

    GtkWidget *subtitle = ui_label("Vui lòng điền chính xác thông tin hành khách theo giấy tờ tùy thân.", "muted");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), subtitle);

    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32);
    gtk_widget_set_margin_top(columns, 16);
    gtk_widget_set_halign(columns, GTK_ALIGN_CENTER);

    GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_size_request(left_col, 560, -1);

    GtkWidget *form_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    add_class(form_card, "content-card");
    gtk_box_append(GTK_BOX(form_card), ui_label("THÔNG TIN HÀNH KHÁCH 1 (NGƯỜI LỚN)", "section-kicker"));

    GtkWidget *name_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    ui_field(name_row, "Họ và tên đệm", "Ví dụ: NGUYỄN VĂN", &app->pass_first_name, FALSE);
    ui_field(name_row, "Tên", "Ví dụ: AN", &app->pass_last_name, FALSE);
    gtk_box_append(GTK_BOX(form_card), name_row);

    GtkWidget *contact_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    ui_field(contact_row, "Số điện thoại liên hệ", "10 chữ số", &app->pass_phone, FALSE);
    ui_field(contact_row, "Email nhận vé điện tử", "ten@email.com", &app->pass_email, FALSE);
    gtk_box_append(GTK_BOX(form_card), contact_row);

    GtkWidget *id_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    ui_field(id_row, "Số CCCD / Hộ chiếu", "12 chữ số", &app->pass_id_card, FALSE);
    ui_field(id_row, "Ngày sinh", "YYYY-MM-DD", &app->pass_birth, FALSE);
    gtk_box_append(GTK_BOX(form_card), id_row);

    gtk_box_append(GTK_BOX(left_col), form_card);

    GtkWidget *addon_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(addon_card, "content-card");
    gtk_box_append(GTK_BOX(addon_card), ui_label("DỊCH VỤ & HÀNH LÝ BỔ SUNG", "section-kicker"));

    GtkWidget *chk1 = gtk_check_button_new_with_label("Hành lý xách tay 12kg tiêu chuẩn (Đã bao gồm miễn phí)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk1), TRUE);
    gtk_widget_set_sensitive(chk1, FALSE);
    gtk_box_append(GTK_BOX(addon_card), chk1);

    GtkWidget *chk2 = gtk_check_button_new_with_label("Gói hành lý ký gửi 23kg (+350.000 đ)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk2), TRUE);
    gtk_box_append(GTK_BOX(addon_card), chk2);

    GtkWidget *chk3 = gtk_check_button_new_with_label("Bảo hiểm chuyến bay toàn diện QCare (+120.000 đ)");
    gtk_box_append(GTK_BOX(addon_card), chk3);

    gtk_box_append(GTK_BOX(left_col), addon_card);
    gtk_box_append(GTK_BOX(columns), left_col);

    GtkWidget *right_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_size_request(right_col, 320, -1);

    GtkWidget *summary = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(summary, "summary-card");
    gtk_box_append(GTK_BOX(summary), ui_label("TÓM TẮT ĐẶT VÉ", "summary-title"));

    app->book_flight_lbl = ui_label("VN 267 · Vietnam Airlines\nHà Nội → TP. Hồ Chí Minh", "flight-route");
    gtk_box_append(GTK_BOX(summary), app->book_flight_lbl);

    app->book_seat_lbl = ui_label("Ghế đã chọn: 12A", "muted");
    gtk_box_append(GTK_BOX(summary), app->book_seat_lbl);

    app->book_price_lbl = ui_label("Giá vé: 2.955.000 đ", "flight-price");
    gtk_box_append(GTK_BOX(summary), app->book_price_lbl);

    GtkWidget *fee_note = ui_label("Thuế & Phí dịch vụ: 350.000 đ", "muted");
    gtk_box_append(GTK_BOX(summary), fee_note);

    gtk_box_append(GTK_BOX(right_col), summary);
    gtk_box_append(GTK_BOX(columns), right_col);
    gtk_box_append(GTK_BOX(page), columns);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(actions, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(actions, 24);

    GtkWidget *back_btn = ui_button("‹ Chọn lại ghế ngồi", "secondary-button",
                                    G_CALLBACK(ui_nav_to_seats), app);
    gtk_box_append(GTK_BOX(actions), back_btn);

    GtkWidget *cont_btn = ui_button("Tiếp tục: Thanh toán an toàn  →", "primary-button",
                                    G_CALLBACK(on_booking_continue), app);
    gtk_box_append(GTK_BOX(actions), cont_btn);

    gtk_box_append(GTK_BOX(page), actions);
    return scroller;
}

static GtkWidget *ui_payment_page(App *app) {
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(page, 48);
    gtk_widget_set_margin_end(page, 48);
    gtk_widget_set_margin_top(page, 24);
    gtk_widget_set_margin_bottom(page, 36);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), page);

    gtk_box_append(GTK_BOX(page), ui_step_indicator(4));

    GtkWidget *title = ui_label("THANH TOÁN AN TOÀN (PAYMENT)", "section-title");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), title);

    GtkWidget *subtitle = ui_label("Giao dịch được bảo mật bởi chuẩn thanh toán quốc tế PCI-DSS.", "muted");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), subtitle);

    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32);
    gtk_widget_set_margin_top(columns, 16);
    gtk_widget_set_halign(columns, GTK_ALIGN_CENTER);

    GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_size_request(left_col, 520, -1);

    GtkWidget *method_tabs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *tab_card = gtk_button_new_with_label("💳  Thẻ Quốc Tế (Visa / Master)");
    add_class(tab_card, "search-tab-active");
    gtk_box_append(GTK_BOX(method_tabs), tab_card);
    GtkWidget *tab_qr = gtk_button_new_with_label("📱  Quét mã VietQR / VNPAY");
    add_class(tab_qr, "secondary-button");
    gtk_box_append(GTK_BOX(method_tabs), tab_qr);
    GtkWidget *tab_atm = gtk_button_new_with_label("🏦  Thẻ ATM Nội Địa");
    add_class(tab_atm, "secondary-button");
    gtk_box_append(GTK_BOX(method_tabs), tab_atm);
    gtk_box_append(GTK_BOX(left_col), method_tabs);

    GtkWidget *card_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    add_class(card_box, "content-card");

    ui_field(card_box, "Số thẻ tín dụng / ghi nợ", "4111  2222  3333  4444", &app->pay_card_num, FALSE);
    ui_field(card_box, "Tên in trên thẻ (không dấu)", "NGUYEN VAN AN", &app->pay_card_holder, FALSE);

    GtkWidget *card_sub = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    ui_field(card_sub, "Ngày hết hạn (MM/YY)", "12/28", &app->pay_card_exp, FALSE);
    ui_field(card_sub, "Mã CVV / CVC", "888", &app->pay_card_cvv, FALSE);
    gtk_box_append(GTK_BOX(card_box), card_sub);

    GtkWidget *chk_agree = gtk_check_button_new_with_label("Tôi đồng ý với Điều kiện vận chuyển và Quy định bảo mật.");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_agree), TRUE);
    gtk_box_append(GTK_BOX(card_box), chk_agree);

    GtkWidget *sec_badge = ui_label("🔒 Thông tin thanh toán được mã hóa 256-bit SSL trực tiếp.", "muted");
    gtk_box_append(GTK_BOX(card_box), sec_badge);

    gtk_box_append(GTK_BOX(left_col), card_box);
    gtk_box_append(GTK_BOX(columns), left_col);

    GtkWidget *right_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_size_request(right_col, 340, -1);

    GtkWidget *summary = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    add_class(summary, "summary-card");
    gtk_box_append(GTK_BOX(summary), ui_label("CHI TIẾT THANH TOÁN", "summary-title"));

    app->pay_flight_lbl = ui_label("VN 267 · Vietnam Airlines\nHà Nội → TP. Hồ Chí Minh", "flight-route");
    gtk_box_append(GTK_BOX(summary), app->pay_flight_lbl);

    app->pay_seat_lbl = ui_label("Hành khách: NGUYỄN VĂN AN · Ghế: 12A", "muted");
    gtk_box_append(GTK_BOX(summary), app->pay_seat_lbl);

    GtkWidget *divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(summary), divider);

    GtkWidget *fee_row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(fee_row1), ui_label("Giá vé cơ bản:", "muted"));
    GtkWidget *fsp1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(fsp1, TRUE);
    gtk_box_append(GTK_BOX(fee_row1), fsp1);
    gtk_box_append(GTK_BOX(fee_row1), ui_label("2.955.000 đ", "flight-number"));
    gtk_box_append(GTK_BOX(summary), fee_row1);

    GtkWidget *fee_row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(fee_row2), ui_label("Thuế & Phí dịch vụ sân bay:", "muted"));
    GtkWidget *fsp2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(fsp2, TRUE);
    gtk_box_append(GTK_BOX(fee_row2), fsp2);
    gtk_box_append(GTK_BOX(fee_row2), ui_label("350.000 đ", "flight-number"));
    gtk_box_append(GTK_BOX(summary), fee_row2);

    GtkWidget *divider2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(summary), divider2);

    GtkWidget *tot_header = ui_label("TỔNG TIỀN PHẢI TRẢ:", "field-caption");
    gtk_box_append(GTK_BOX(summary), tot_header);
    app->pay_total_lbl = ui_label("3.305.000 đ", "price-total");
    gtk_box_append(GTK_BOX(summary), app->pay_total_lbl);

    gtk_box_append(GTK_BOX(right_col), summary);
    gtk_box_append(GTK_BOX(columns), right_col);
    gtk_box_append(GTK_BOX(page), columns);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(actions, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(actions, 24);

    GtkWidget *back_btn = ui_button("‹ Quay lại chỉnh thông tin", "secondary-button",
                                    G_CALLBACK(ui_nav_to_booking), app);
    gtk_box_append(GTK_BOX(actions), back_btn);

    GtkWidget *pay_btn = ui_button("XÁC NHẬN THANH TOÁN  💳", "primary-button",
                                   G_CALLBACK(on_payment_confirm), app);
    gtk_box_append(GTK_BOX(actions), pay_btn);

    gtk_box_append(GTK_BOX(page), actions);
    return scroller;
}

static GtkWidget *ui_confirm_page(App *app) {
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(page, 48);
    gtk_widget_set_margin_end(page, 48);
    gtk_widget_set_margin_top(page, 24);
    gtk_widget_set_margin_bottom(page, 36);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), page);

    gtk_box_append(GTK_BOX(page), ui_step_indicator(5));

    GtkWidget *succ_circle = ui_label("✔", "success-circle");
    gtk_widget_set_halign(succ_circle, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(succ_circle, 64, 64);
    gtk_box_append(GTK_BOX(page), succ_circle);

    GtkWidget *title = ui_label("ĐẶT VÉ VÀ THANH TOÁN THÀNH CÔNG!", "section-title");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), title);

    GtkWidget *subtitle = ui_label("Cảm ơn bạn đã lựa chọn QAirline. Vé máy bay điện tử đã được xác nhận thành công.", "muted");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), subtitle);

    GtkWidget *ticket = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    add_class(ticket, "boarding-pass-card");
    gtk_widget_set_size_request(ticket, 700, -1);
    gtk_widget_set_halign(ticket, GTK_ALIGN_CENTER);

    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *brand = ui_label("✈  QAIRLINE  ·  BOARDING PASS (THẺ LÊN TÀU BAY)", "summary-title");
    gtk_box_append(GTK_BOX(top_row), brand);
    GtkWidget *sp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(sp, TRUE);
    gtk_box_append(GTK_BOX(top_row), sp);
    app->conf_pnr_lbl = ui_label("MÃ PNR: LTM-849201", "pnr-box");
    gtk_box_append(GTK_BOX(top_row), app->conf_pnr_lbl);
    gtk_box_append(GTK_BOX(ticket), top_row);

    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(ticket), sep1);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 36);

    gtk_grid_attach(GTK_GRID(grid), ui_label("TÊN HÀNH KHÁCH", "field-caption"), 0, 0, 1, 1);
    app->conf_pass_name_lbl = ui_label("NGUYỄN VĂN AN", "flight-number");
    gtk_grid_attach(GTK_GRID(grid), app->conf_pass_name_lbl, 0, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), ui_label("CHUYẾN BAY", "field-caption"), 1, 0, 1, 1);
    app->conf_flight_lbl = ui_label("VN 267 · Vietnam Airlines", "flight-number");
    gtk_grid_attach(GTK_GRID(grid), app->conf_flight_lbl, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), ui_label("HẠNG VÉ", "field-caption"), 2, 0, 1, 1);
    GtkWidget *class_lbl = ui_label("Phổ Thông Tiêu Chuẩn", "flight-number");
    gtk_grid_attach(GTK_GRID(grid), class_lbl, 2, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), ui_label("HÀNH TRÌNH BAY", "field-caption"), 0, 2, 1, 1);
    app->conf_route_lbl = ui_label("Hà Nội (HAN) ➔ TP. Hồ Chí Minh (SGN)", "flight-route");
    gtk_grid_attach(GTK_GRID(grid), app->conf_route_lbl, 0, 3, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), ui_label("NGÀY KHỞI HÀNH", "field-caption"), 1, 2, 1, 1);
    app->conf_date_lbl = ui_label("2026-11-21", "flight-number");
    gtk_grid_attach(GTK_GRID(grid), app->conf_date_lbl, 1, 3, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), ui_label("SỐ GHẾ (SEAT)", "field-caption"), 2, 2, 1, 1);
    app->conf_seat_lbl = ui_label("12A", "price-total");
    gtk_grid_attach(GTK_GRID(grid), app->conf_seat_lbl, 2, 3, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), ui_label("CỬA RA MÁY BAY (GATE)", "field-caption"), 0, 4, 1, 1);
    GtkWidget *gate_lbl = ui_label("GATE 04", "flight-number");
    gtk_grid_attach(GTK_GRID(grid), gate_lbl, 0, 5, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), ui_label("GIỜ LÊN TÀU (BOARDING)", "field-caption"), 1, 4, 1, 1);
    GtkWidget *board_lbl = ui_label("21:30 (30 phút trước giờ bay)", "flight-number");
    gtk_grid_attach(GTK_GRID(grid), board_lbl, 1, 5, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), ui_label("TRẠNG THÁI", "field-caption"), 2, 4, 1, 1);
    GtkWidget *stat_lbl = ui_label("ĐÃ XÁC NHẬN", "step-item step-done");
    gtk_grid_attach(GTK_GRID(grid), stat_lbl, 2, 5, 1, 1);

    gtk_box_append(GTK_BOX(ticket), grid);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(ticket), sep2);

    GtkWidget *barcode = ui_label("||| | ||||| || |||||| | |||| ||| ||||||| ||||| |||||| | ||||", "barcode-text");
    gtk_widget_set_halign(barcode, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(ticket), barcode);

    GtkWidget *notice = ui_label("Quý khách vui lòng có mặt tại cửa khởi hành trước 40 phút và xuất trình CCCD/Hộ chiếu cùng thẻ lên tàu này.", "muted");
    gtk_widget_set_halign(notice, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(ticket), notice);

    gtk_box_append(GTK_BOX(page), ticket);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign(actions, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(actions, 16);

    GtkWidget *dl_btn = ui_button("🖨  In / Tải Thẻ Lên Máy Bay (PDF)", "secondary-button",
                                  G_CALLBACK(on_download_ticket), app);
    gtk_box_append(GTK_BOX(actions), dl_btn);

    GtkWidget *home_btn = ui_button("🏠  Về Trang Chủ", "primary-button",
                                    G_CALLBACK(on_return_home), app);
    gtk_box_append(GTK_BOX(actions), home_btn);

    gtk_box_append(GTK_BOX(page), actions);
    return scroller;
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
    gtk_box_append(GTK_BOX(app->nav), ui_button("Tài khoản", "nav-link", G_CALLBACK(account_clicked), app));
    gtk_box_append(GTK_BOX(header), app->nav);

    GtkWidget *account = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_valign(account, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(account, GTK_ALIGN_END);

    GtkWidget *search_btn = ui_button("⌕", "header-search",
                                      G_CALLBACK(ui_header_search_clicked), app);
    gtk_widget_set_valign(search_btn, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(account), search_btn);

    app->header_login = ui_button("Đăng nhập", "header-btn-login",
                                     G_CALLBACK(ui_nav_clicked), app);
    g_object_set_data(G_OBJECT(app->header_login), "page", (gpointer)"login");
    gtk_widget_set_valign(app->header_login, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(account), app->header_login);

    app->header_signup = ui_button("Đăng ký", "header-btn-signup",
                                      G_CALLBACK(ui_nav_clicked), app);
    g_object_set_data(G_OBJECT(app->header_signup), "page", (gpointer)"signup");
    gtk_widget_set_valign(app->header_signup, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(account), app->header_signup);

    app->header_user=ui_label("", "muted");
    gtk_widget_set_valign(app->header_user,GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(account),app->header_user);
    app->header_logout = ui_button("Đăng xuất", "header-btn-login",
                                      G_CALLBACK(logout_clicked), app);
    gtk_box_append(GTK_BOX(account), app->header_logout);
    set_auth_header(app);

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
    app->flight_error=ui_label("","form-error");
    gtk_widget_set_visible(app->flight_error,FALSE);
    gtk_box_append(GTK_BOX(body),app->flight_error);
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
    GtkWidget *account_page = ui_content_page("TÀI KHOẢN", "Thông tin cá nhân",
        "Thông tin này dùng để điền form liên hệ và hành khách.", &body);
    app->account_error=ui_label("","form-error");
    gtk_widget_set_visible(app->account_error,FALSE);
    gtk_box_append(GTK_BOX(body),app->account_error);
    ui_field(body,"Tên","Tên",&app->profile_first,FALSE);
    ui_field(body,"Họ","Họ",&app->profile_last,FALSE);
    ui_field(body,"Số điện thoại","10 chữ số",&app->profile_phone,FALSE);
    ui_field(body,"Ngày sinh","YYYY-MM-DD",&app->profile_birth,FALSE);
    gtk_box_append(GTK_BOX(body), ui_label("Giới tính", "field-caption"));
    app->profile_gender=gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->profile_gender),"Male","Nam");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->profile_gender),"Female","Nữ");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->profile_gender),"Other","Khác");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->profile_gender),2);
    gtk_box_append(GTK_BOX(body),app->profile_gender);
    ui_field(body,"CCCD","CCCD",&app->profile_id,FALSE);
    ui_field(body,"Địa chỉ","Địa chỉ",&app->profile_address,FALSE);
    gtk_box_append(GTK_BOX(body),ui_button("Lưu hồ sơ","primary-button",G_CALLBACK(save_profile_clicked),app));
    gtk_box_append(GTK_BOX(body),ui_label("Đổi mật khẩu", "section-title"));
    ui_field(body,"Mật khẩu cũ","Mật khẩu hiện tại",&app->password_old,TRUE);
    ui_field(body,"Mật khẩu mới","Ít nhất 8 ký tự, có chữ và số",&app->password_new,TRUE);
    gtk_box_append(GTK_BOX(body),ui_button("Đổi mật khẩu","secondary-button",G_CALLBACK(change_password_clicked),app));
    GtkWidget *account_scroll=gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(account_scroll),account_page);
    gtk_stack_add_named(GTK_STACK(app->stack),account_scroll,"account");

    gtk_stack_add_named(GTK_STACK(app->stack), ui_seats_page(app), "seats");
    gtk_stack_add_named(GTK_STACK(app->stack), ui_booking_page(app), "booking");
    gtk_stack_add_named(GTK_STACK(app->stack), ui_payment_page(app), "payment");
    gtk_stack_add_named(GTK_STACK(app->stack), ui_confirm_page(app), "confirm");

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
    request(app, MSG_AIRPORTS, g_strdup("{}"), show_airports);
    if (app->session_id) {
        JsonBuilder *b = object_builder();
        add_string(b, "sessionId", app->session_id);
        json_builder_end_object(b);
        request(app, MSG_RESUME, make_json(b), show_resume);
    }
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
    char *saved_path = session_path();
    char *saved_id = NULL;
    if (g_file_get_contents(saved_path, &saved_id, NULL, NULL) && strlen(saved_id) == 64) {
        app.session_id = g_strdup(saved_id);
        net_client_set_session(app.network, app.session_id);
    }
    g_free(saved_id); g_free(saved_path);
    app.queue = g_async_queue_new();
    app.worker = g_thread_new("tcp-client", network_worker, &app);
    net_client_set_push_handler(app.network, push_received, &app);
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
    g_free(app.session_id);
    g_object_unref(gtk_app);
    return rc;
}
