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

// Global variables
int client_socket;
char current_username[50];
char current_room[50];
int logged_in = 0;

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
void* receive_messages(void* arg);
void show_error(const char* message);
void show_info(const char* message);
void append_to_chat(const char* message);
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
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
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

    // Send message command
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "MESSAGE: %s %s", current_username, message);
    send(client_socket, buffer, strlen(buffer), 0);

    // Add message to chat
    char chat_message[BUFFER_SIZE + 100];
    sprintf(chat_message, "%s: %s", current_username, message);
    append_to_chat(chat_message);

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
void* receive_messages(void* arg) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';

        if (strncmp(buffer, "REGISTER_SUCCESS", strlen("REGISTER_SUCCESS")) == 0) {
            printf("REGISTER_SUCCESS");
            //gdk_threads_add_idle((GSourceFunc)gtk_stack_set_visible_child_name, GTK_STACK(stack));
            gtk_stack_set_visible_child_name(GTK_STACK(stack), "login_page");
            gdk_threads_add_idle((GSourceFunc)show_info, g_strdup("Registration successful"));
        } else if (strncmp(buffer, "REGISTER_FAIL", strlen("REGISTER_FAIL")) == 0) {
            gdk_threads_add_idle((GSourceFunc)show_error, g_strdup("Registration failed"));
        } else if (strncmp(buffer, "LOGIN_SUCCESS", strlen("LOGIN_SUCCESS")) == 0) {
            logged_in = 1;
            // Extract username
            char username[50];
            sscanf(buffer, "LOGIN_SUCCESS %s", username);
            printf("%s\n", username);
            strcpy(current_username, username);
            strcpy(current_room, "General");

            // gdk_threads_add_idle((GSourceFunc)gtk_stack_set_visible_child_name, GTK_STACK(stack));
            gtk_stack_set_visible_child_name(GTK_STACK(stack), "chat_page");
            //
            char status[100];
            sprintf(status, "Logged in as: %s | Room: %s", current_username, current_room);
            //gdk_threads_add_idle((GSourceFunc)gtk_label_set_text, status_label);
            gtk_label_set_text(GTK_LABEL(status_label), status);

            gdk_threads_add_idle((GSourceFunc)clear_chat, NULL);
            gdk_threads_add_idle((GSourceFunc)append_to_chat, g_strdup("Welcome to the chat!\n"));

        } else if (strncmp(buffer, "LOGIN_FAIL", strlen("LOGIN_FAIL")) == 0) {
            gdk_threads_add_idle((GSourceFunc)show_error, g_strdup("Login failed"));
        } else if (strncmp(buffer, "LOGOUT_SUCCESS", strlen("LOGOUT_SUCCESS")) == 0) {
            logged_in = 0;
            //gdk_threads_add_idle((GSourceFunc)gtk_stack_set_visible_child_name, GTK_STACK(stack));
            gtk_stack_set_visible_child_name(GTK_STACK(stack), "login_page");
        } else if (strncmp(buffer, "JOIN_SUCCESS", strlen("JOIN_SUCCESS")) == 0) {
            char room[50];
            sscanf(buffer, "JOIN_SUCCESS %s", room);
            strcpy(current_room, room);

            char status[100];
            sprintf(status, "Logged in as: %s | Room: %s", current_username, current_room);
            //gdk_threads_add_idle((GSourceFunc)gtk_label_set_text, status_label);
            gtk_label_set_text(GTK_LABEL(status_label), status);

            gdk_threads_add_idle((GSourceFunc)clear_chat, NULL);

            char message[100];
            sprintf(message, "Joined room: %s\n", current_room);
            gdk_threads_add_idle((GSourceFunc)append_to_chat, g_strdup(message));
        } else if (strncmp(buffer, "CREATE_SUCCESS", strlen("CREATE_SUCCESS")) == 0) {
            char room[50];
            sscanf(buffer, "CREATE_SUCCESS %s", room);
            strcpy(current_room, room);

            char status[100];
            sprintf(status, "Logged in as: %s | Room: %s", current_username, current_room);
            //gdk_threads_add_idle((GSourceFunc)gtk_label_set_text, status_label);
            gtk_label_set_text(GTK_LABEL(status_label), status);

            gdk_threads_add_idle((GSourceFunc)clear_chat, NULL);

            char message[100];
            sprintf(message, "Created and joined room: %s\n", current_room);
            gdk_threads_add_idle((GSourceFunc)append_to_chat, g_strdup(message));
        } else if (strncmp(buffer, "MESSAGE", strlen("MESSAGE")) == 0) {
            char sender[50];
            char message[BUFFER_SIZE];
            char full_message[BUFFER_SIZE];
            printf("%s\n", buffer);
            sscanf(buffer, "MESSAGE: %s %[^\n]", sender, message);
            sprintf(full_message, "%s: %s",sender, message);
            printf("%s\n", full_message);

            gdk_threads_add_idle((GSourceFunc)append_to_chat, g_strdup(full_message));
            // if (strcmp(room, current_room) == 0) {
            //     gdk_threads_add_idle((GSourceFunc)append_to_chat, g_strdup(message));
            // }
        } else if (strncmp(buffer, "SYSTEM", strlen("SYSTEM")) == 0) {
            char room[50];
            char message[BUFFER_SIZE];
            sscanf(buffer, "SYSTEM %s %[^\n]", room, message);

            if (strcmp(room, current_room) == 0) {
                char system_message[BUFFER_SIZE];
                sprintf(system_message, "[SYSTEM] %s", message);
                gdk_threads_add_idle((GSourceFunc)append_to_chat, g_strdup(system_message));
            }
        }
    }

    // Connection closed
    logged_in = 0;
    close(client_socket);
    client_socket = 0;

    gdk_threads_add_idle((GSourceFunc)show_error, g_strdup("Disconnected from server"));
    //gdk_threads_add_idle((GSourceFunc)gtk_stack_set_visible_child_name, GTK_STACK(stack));
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "login_page");

    return NULL;
}

// Show error message
void show_error(const char* message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                             GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_CLOSE,
                                             "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Show info message
void show_info(const char* message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                             GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_INFO,
                                             GTK_BUTTONS_CLOSE,
                                             "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Append message to chat
void append_to_chat(const char* message) {
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