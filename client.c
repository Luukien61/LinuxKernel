#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8888
#define BUFFER_SIZE 2048
#define FILE_BUFFER_SIZE 10485760

// Global variables
int client_socket;
char current_username[50];
char current_room[50];
int logged_in = 0;
int downloading = 0;
char download_filename[100] = "";

#define MAX_CLIENTS 100
#define BUFFER_SIZE 2048
#define MAX_USERNAME 50
#define MAX_PASSWORD 50

typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} User;

typedef struct {
    int socket;
    char username[MAX_USERNAME];
    int is_logged_in;
} Client;

typedef struct {
    Client *clients[MAX_CLIENTS];
    int client_count;
    pthread_mutex_t clients_mutex;
} ClientList;

// GTK widgets
GtkWidget *window;
GtkWidget *stack;
GtkWidget *login_username_entry;
GtkWidget *login_password_entry;
GtkWidget *register_username_entry;
GtkWidget *register_password_entry;
GtkWidget *chat_text_view;
GtkWidget *message_entry;
GtkWidget *room_entry;
GtkTextBuffer *chat_buffer;
GtkWidget *status_label;

// Function prototypes
void *receive_messages(void *arg);

void show_error(const char *message);

void show_info(const char *message);

void append_to_chat(const char *message);

void clear_chat();

// Connect to server
int connect_to_server() {
    struct sockaddr_in server_addr;

    // Create socket
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        show_error("Socket creation error");
        return 0;
    }

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convert IP address to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        show_error("Invalid address/Address not supported");
        return 0;
    }

    // Connect to server
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        show_error("Connection failed");
        return 0;
    }

    // Start message receiving thread
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, receive_messages, NULL) != 0) {
        show_error("Thread creation failed");
        close(client_socket);
        return 0;
    }
    pthread_detach(thread_id);

    return 1;
}

// Register button click handler
void on_register_button_clicked(GtkWidget *widget, gpointer data) {
    const char *username = gtk_entry_get_text(GTK_ENTRY(register_username_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(register_password_entry));

    if (strlen(username) == 0 || strlen(password) == 0) {
        show_error("Username and password cannot be empty");
        return;
    }

    // Connect to server if not already connected
    if (client_socket <= 0) {
        if (!connect_to_server()) {
            return;
        }
    }

    // Send register command
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "REGISTER %s %s", username, password);
    send(client_socket, buffer, strlen(buffer), 0);
}

// Login button click handler
void on_login_button_clicked(GtkWidget *widget, gpointer data) {
    const char *username = gtk_entry_get_text(GTK_ENTRY(login_username_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(login_password_entry));

    if (strlen(username) == 0 || strlen(password) == 0) {
        show_error("Username and password cannot be empty");
        return;
    }

    // Connect to server if not already connected
    if (client_socket <= 0) {
        if (!connect_to_server()) {
            return;
        }
    }

    // Send login command
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "LOGIN %s %s", username, password);
    send(client_socket, buffer, strlen(buffer), 0);

    // Response will be handled by receive_messages thread
}

// Send message button click handler
void on_send_button_clicked(GtkWidget *widget, gpointer data) {
    const char *message = gtk_entry_get_text(GTK_ENTRY(message_entry));

    if (strlen(message) == 0) {
        return;
    }

    if (!logged_in) {
        show_error("You are not logged in");
        return;
    }

    char command[20], filename[100];
    if (sscanf(message, "%s %s", command, filename) == 2 && strcmp(command, "download") == 0) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, BUFFER_SIZE, "DOWNLOAD %s", filename);
        if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
            perror("Failed to send DOWNLOAD command");
            return;
        }
        printf("Sent DOWNLOAD request for %s\n", filename);
        downloading = 1;
        strcpy(download_filename, filename); // Lưu tên file để xử lý trong receive_messages
        gdk_threads_add_idle((GSourceFunc) append_to_chat, g_strdup("Downloading...\n"));
    } else {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, BUFFER_SIZE, "MESSAGE: %s: %s", current_username, message);
        send(client_socket, buffer, strlen(buffer), 0);

        char chat_message[BUFFER_SIZE + 100];
        snprintf(chat_message, BUFFER_SIZE + 100, "You: %s", message);
        gdk_threads_add_idle((GSourceFunc) append_to_chat, g_strdup(chat_message));
    }

    // Clear message entry
    gtk_entry_set_text(GTK_ENTRY(message_entry), "");
}

// Join room button click handler
void on_join_button_clicked(GtkWidget *widget, gpointer data) {
    const char *room = gtk_entry_get_text(GTK_ENTRY(room_entry));

    if (strlen(room) == 0) {
        show_error("Room name cannot be empty");
        return;
    }

    if (!logged_in) {
        show_error("You are not logged in");
        return;
    }

    // Send join command
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "JOIN %s", room);
    send(client_socket, buffer, strlen(buffer), 0);

    // Response will be handled by receive_messages thread
}

// Create room button click handler
void on_create_button_clicked(GtkWidget *widget, gpointer data) {
    const char *room = gtk_entry_get_text(GTK_ENTRY(room_entry));

    if (strlen(room) == 0) {
        show_error("Room name cannot be empty");
        return;
    }

    if (!logged_in) {
        show_error("You are not logged in");
        return;
    }

    // Send create command
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "CREATE %s", room);
    send(client_socket, buffer, strlen(buffer), 0);

    // Response will be handled by receive_messages thread
}

// Logout button click handler
void on_logout_button_clicked(GtkWidget *widget, gpointer data) {
    if (!logged_in) {
        show_error("You are not logged in");
        return;
    }

    // Send logout command
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "LOGOUT");
    send(client_socket, buffer, strlen(buffer), 0);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "login_page");
    // Response will be handled by receive_messages thread
}

// Switch to register page
void on_goto_register_clicked(GtkWidget *widget, gpointer data) {
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "register_page");
}

// Switch to login page
void on_goto_login_clicked(GtkWidget *widget, gpointer data) {
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "login_page");
}

// Receive messages from server
void *receive_messages(void *arg) {
    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        perror("Failed to allocate buffer");
        return NULL;
    }
    int bytes_read;
    static size_t file_size = 0;
    static size_t total_received = 0;
    static FILE *file = NULL;

    while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Raw buffer: [%s]\n", buffer); // In buffer thô để debug

        if (downloading && strlen(download_filename) > 0) {
            // Giữ nguyên logic tải file
            if (file_size == 0) {
                if (bytes_read == sizeof(size_t)) {
                    memcpy(&file_size, buffer, sizeof(size_t));
                    printf("File size received: %zu bytes\n", file_size);

                    if (file_size == 0) {
                        show_error("File not found on server");
                        downloading = 0;
                        download_filename[0] = '\0';
                        continue;
                    }

                    system("mkdir -p downloads");
                    char filepath[BUFFER_SIZE];
                    snprintf(filepath, BUFFER_SIZE, "downloads/%s", download_filename);
                    file = fopen(filepath, "wb");
                    if (!file) {
                        perror("Cannot create local file");
                        downloading = 0;
                        download_filename[0] = '\0';
                        continue;
                    }
                } else {
                    printf("Invalid file size data: %d bytes\n", bytes_read);
                    downloading = 0;
                    download_filename[0] = '\0';
                }
            } else {
                fwrite(buffer, 1, bytes_read, file);
                total_received += bytes_read;
                printf("Received %d bytes, total: %zu/%zu\n", bytes_read, total_received, file_size);

                if (total_received >= file_size) {
                    fclose(file);
                    file = NULL;
                    char message[BUFFER_SIZE];
                    snprintf(message, BUFFER_SIZE, "File %s downloaded (%zu bytes)\n", download_filename,
                             total_received);
                    gdk_threads_add_idle((GSourceFunc) append_to_chat, g_strdup(message));
                    downloading = 0;
                    download_filename[0] = '\0';
                    file_size = 0;
                    total_received = 0;
                }
            }
            continue;
        }

        // Xử lý từng dòng trong buffer
        char *line = buffer;
        while (line && *line) {
            char *next_line = strchr(line, '\n');
            if (next_line) {
                *next_line = '\0'; // Tách dòng
                next_line++;
                if (*next_line == '\0') next_line = NULL; // Ngăn next_line trỏ vào rỗng
            }

            printf("Processing line: %s\n", line);

            if (strncmp(line, "MESSAGE", strlen("MESSAGE")) == 0) {
                char sender[50], message[BUFFER_SIZE], full_message[BUFFER_SIZE];
                if (sscanf(line, "MESSAGE: %49[^:]: %[^\n]", sender, message) == 2) {
                    printf("Sender: %s, Message: %s\n", sender, message);
                    if (strncasecmp(sender, current_username, strlen(current_username)) == 0) {
                        strcpy(sender, "You");
                    }
                    sprintf(full_message, "%s: %s", sender, message);
                    gdk_threads_add_idle((GSourceFunc) append_to_chat, g_strdup(full_message));
                } else {
                    printf("Failed to parse MESSAGE: %s\n", line);
                }
            } else if (strncmp(line, "REGISTER_SUCCESS", strlen("REGISTER_SUCCESS")) == 0) {
                gtk_stack_set_visible_child_name(GTK_STACK(stack), "login_page");
            } else if (strncmp(line, "REGISTER_FAIL", strlen("REGISTER_FAIL")) == 0) {
                gdk_threads_add_idle((GSourceFunc) show_error, g_strdup("Registration failed"));
            } else if (strncmp(line, "LOGIN_SUCCESS", strlen("LOGIN_SUCCESS")) == 0) {
                logged_in = 1;
                char username[50];
                sscanf(line, "LOGIN_SUCCESS %s", username);
                strcpy(current_username, username);
                strcpy(current_room, "General");
                gtk_stack_set_visible_child_name(GTK_STACK(stack), "chat_page");
                char status[100];
                sprintf(status, "Logged in as: %s | Room: %s", current_username, current_room);
                gtk_label_set_text(GTK_LABEL(status_label), status);
                gdk_threads_add_idle((GSourceFunc) clear_chat, NULL);
                gdk_threads_add_idle((GSourceFunc) append_to_chat, g_strdup("Welcome to the chat!\n"));
            } else if (strncmp(line, "LOGIN_FAIL", strlen("LOGIN_FAIL")) == 0) {
                gdk_threads_add_idle((GSourceFunc) show_error, g_strdup("Login failed"));
            } else if (strncmp(line, "LOGOUT_SUCCESS", strlen("LOGOUT_SUCCESS")) == 0) {
                logged_in = 0;
            } else if (strncmp(line, "SYSTEM", strlen("SYSTEM")) == 0) {
                char room[50], message[BUFFER_SIZE];
                if (sscanf(line, "SYSTEM %s %[^\n]", room, message) == 2) {
                    if (strcmp(room, current_room) == 0) {
                        char system_message[BUFFER_SIZE];
                        sprintf(system_message, "[SYSTEM] %s", message);
                        gdk_threads_add_idle((GSourceFunc) append_to_chat, g_strdup(system_message));
                    }
                }
            }

            line = next_line;
        }
    }

    logged_in = 0;
    close(client_socket);
    client_socket = 0;

    gdk_threads_add_idle((GSourceFunc) show_error, g_strdup("Disconnected from server"));
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "login_page");

    free(buffer);
    return NULL;
}

// Show error message
void show_error(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_CLOSE,
                                               "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Show info message
void show_info(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_CLOSE,
                                               "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Append message to chat
void append_to_chat(const char *message) {
    GtkTextIter iter;

    gtk_text_buffer_get_end_iter(chat_buffer, &iter);
    gtk_text_buffer_insert(chat_buffer, &iter, message, -1);
    gtk_text_buffer_insert(chat_buffer, &iter, "\n", -1);

    // Scroll to bottom
    GtkTextMark *mark = gtk_text_buffer_get_insert(chat_buffer);
    gtk_text_buffer_get_end_iter(chat_buffer, &iter);
    gtk_text_buffer_place_cursor(chat_buffer, &iter);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(chat_text_view), mark, 0.0, TRUE, 0.0, 1.0);
}

// Clear chat
void clear_chat() {
    gtk_text_buffer_set_text(chat_buffer, "", -1);
}

void send_file_to_server(const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        show_error("Cannot open file");
        return;
    }

    // Lấy tên file từ đường dẫn
    const char *filename = strrchr(filepath, '/');
    if (!filename) filename = filepath;
    else filename++; // Bỏ qua ký tự '/'

    // Lấy kích thước file
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Gửi lệnh UPLOAD
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "UPLOAD %s %zu", filename, file_size);
    if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
        perror("Failed to send UPLOAD command");
        fclose(file);
        return;
    }

    char *file_buffer = malloc(FILE_BUFFER_SIZE);
    if (!file_buffer) {
        perror("Failed to allocate file buffer");
        fclose(file);
        return;
    }

    // Gửi dữ liệu file
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, FILE_BUFFER_SIZE, file)) > 0) {
        if (send(client_socket, file_buffer, bytes_read, 0) < 0) {
            perror("Failed to send file data");
            free(file_buffer);
            fclose(file);
            return;
        }
    }

    free(file_buffer);
    fclose(file);

    // Trễ 100ms để server xử lý xong file
    usleep(100000); // 100ms

    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "MESSAGE: %s: [FILE] %s\n", current_username, filename);
    if (send(client_socket, message, strlen(message), 0) < 0) {
        perror("Failed to send file notification");
        return;
    }

    // Thêm thông báo vào giao diện cục bộ
    char chat_message[BUFFER_SIZE];
    snprintf(chat_message, BUFFER_SIZE, "You: [FILE] %s\n", filename);
    gdk_threads_add_idle((GSourceFunc) append_to_chat, g_strdup(chat_message));
}

void on_upload_button_clicked(GtkWidget *widget, gpointer data) {
    if (!logged_in) {
        show_error("You are not logged in");
        return;
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select File",
                                                    GTK_WINDOW(window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "Cancel", GTK_RESPONSE_CANCEL,
                                                    "Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filepath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        send_file_to_server(filepath);
        g_free(filepath);
    }
    gtk_widget_destroy(dialog);
}

void download_file(const char *filename) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "DOWNLOAD %s", filename);
    send(client_socket, buffer, strlen(buffer), 0);

    size_t file_size;
    recv(client_socket, &file_size, sizeof(size_t), 0);

    if (file_size == 0) {
        show_error("File not found on server");
        return;
    }

    char filepath[BUFFER_SIZE];
    snprintf(filepath, BUFFER_SIZE, "downloads/%s", filename);
    FILE *file = fopen(filepath, "wb");
    if (!file) {
        show_error("Cannot create local file");
        return;
    }

    char *file_buffer = malloc(FILE_BUFFER_SIZE);
    if (!file_buffer) {
        perror("Failed to allocate file buffer");
        fclose(file);
        return;
    }

    size_t total_received = 0;
    int bytes_received;
    while (total_received < file_size &&
           (bytes_received = recv(client_socket, file_buffer, FILE_BUFFER_SIZE, 0)) > 0) {
        fwrite(file_buffer, 1, bytes_received, file);
        total_received += bytes_received;
    }

    free(file_buffer);
    fclose(file);
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "File %s downloaded (%zu bytes)\n", filename, total_received);
    gdk_threads_add_idle((GSourceFunc) append_to_chat, g_strdup(message));
}

// Create UI
static void activate(GtkApplication *app, gpointer user_data) {
    // Window
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Chat Application");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    // Stack
    stack = gtk_stack_new();
    gtk_container_add(GTK_CONTAINER(window), stack);

    // Login page
    GtkWidget *login_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(login_box), 20);
    gtk_stack_add_named(GTK_STACK(stack), login_box, "login_page");

    GtkWidget *login_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(login_label), "<span size='xx-large' weight='bold'>Login</span>");
    gtk_container_add(GTK_CONTAINER(login_box), login_label);

    GtkWidget *login_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(login_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(login_grid), 10);
    gtk_widget_set_halign(login_grid, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(login_box), login_grid);

    // Username
    GtkWidget *username_label = gtk_label_new("Username:");
    gtk_grid_attach(GTK_GRID(login_grid), username_label, 0, 0, 1, 1);

    login_username_entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(login_username_entry), 30);
    gtk_grid_attach(GTK_GRID(login_grid), login_username_entry, 1, 0, 1, 1);

    // Password
    GtkWidget *password_label = gtk_label_new("Password:");
    gtk_grid_attach(GTK_GRID(login_grid), password_label, 0, 1, 1, 1);

    login_password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(login_password_entry), FALSE);
    gtk_entry_set_width_chars(GTK_ENTRY(login_password_entry), 30);
    gtk_grid_attach(GTK_GRID(login_grid), login_password_entry, 1, 1, 1, 1);

    // Login button
    GtkWidget *login_button = gtk_button_new_with_label("Login");
    g_signal_connect(login_button, "clicked", G_CALLBACK(on_login_button_clicked), NULL);
    gtk_grid_attach(GTK_GRID(login_grid), login_button, 1, 2, 1, 1);

    // Register link
    GtkWidget *register_link = gtk_button_new_with_label("Don't have an account? Register");
    gtk_button_set_relief(GTK_BUTTON(register_link), GTK_RELIEF_NONE);
    g_signal_connect(register_link, "clicked", G_CALLBACK(on_goto_register_clicked), NULL);
    gtk_grid_attach(GTK_GRID(login_grid), register_link, 1, 3, 1, 1);

    // Register page
    GtkWidget *register_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(register_box), 20);
    gtk_stack_add_named(GTK_STACK(stack), register_box, "register_page");

    GtkWidget *register_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(register_label), "<span size='xx-large' weight='bold'>Register</span>");
    gtk_container_add(GTK_CONTAINER(register_box), register_label);

    GtkWidget *register_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(register_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(register_grid), 10);
    gtk_widget_set_halign(register_grid, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(register_box), register_grid);

    // Username
    GtkWidget *reg_username_label = gtk_label_new("Username:");
    gtk_grid_attach(GTK_GRID(register_grid), reg_username_label, 0, 0, 1, 1);

    register_username_entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(register_username_entry), 30);
    gtk_grid_attach(GTK_GRID(register_grid), register_username_entry, 1, 0, 1, 1);

    // Password
    GtkWidget *reg_password_label = gtk_label_new("Password:");
    gtk_grid_attach(GTK_GRID(register_grid), reg_password_label, 0, 1, 1, 1);

    register_password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(register_password_entry), FALSE);
    gtk_entry_set_width_chars(GTK_ENTRY(register_password_entry), 30);
    gtk_grid_attach(GTK_GRID(register_grid), register_password_entry, 1, 1, 1, 1);

    // Register button
    GtkWidget *register_button = gtk_button_new_with_label("Register");
    g_signal_connect(register_button, "clicked", G_CALLBACK(on_register_button_clicked), NULL);
    gtk_grid_attach(GTK_GRID(register_grid), register_button, 1, 2, 1, 1);

    // Login link
    GtkWidget *login_link = gtk_button_new_with_label("Already have an account? Login");
    gtk_button_set_relief(GTK_BUTTON(login_link), GTK_RELIEF_NONE);
    g_signal_connect(login_link, "clicked", G_CALLBACK(on_goto_login_clicked), NULL);
    gtk_grid_attach(GTK_GRID(register_grid), login_link, 1, 3, 1, 1);

    // Chat page
    GtkWidget *chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(chat_box), 20);
    gtk_stack_add_named(GTK_STACK(stack), chat_box, "chat_page");

    // Status bar
    status_label = gtk_label_new("Not logged in");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0);
    gtk_container_add(GTK_CONTAINER(chat_box), status_label);

    // Room controls
    GtkWidget *room_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_add(GTK_CONTAINER(chat_box), room_box);


    GtkWidget *logout_button = gtk_button_new_with_label("Logout");
    g_signal_connect(logout_button, "clicked", G_CALLBACK(on_logout_button_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(room_box), logout_button);

    // Chat area
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_container_add(GTK_CONTAINER(chat_box), scrolled_window);

    chat_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat_text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_text_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled_window), chat_text_view);

    chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_text_view));

    // Message input
    GtkWidget *message_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_add(GTK_CONTAINER(chat_box), message_box);

    message_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(message_entry), "Type a message...");
    gtk_widget_set_hexpand(message_entry, TRUE);
    gtk_container_add(GTK_CONTAINER(message_box), message_entry);

    GtkWidget *send_button = gtk_button_new_with_label("Send");
    g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_button_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(message_box), send_button);

    GtkWidget *upload_button = gtk_button_new_with_label("Upload File");
    g_signal_connect(upload_button, "clicked", G_CALLBACK(on_upload_button_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(message_box), upload_button);


    // Enter key to send message
    g_signal_connect(message_entry, "activate", G_CALLBACK(on_send_button_clicked), NULL);

    // Show all
    gtk_widget_show_all(window);

    // Start on login page
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "login_page");
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.kienluu.chat-2", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

//export DISPLAY=:0
