#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "AES.h"
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 2048
#define FILE_BUFFER_SIZE 10485760
#define MAX_USERNAME 64
#define MAX_PASSWORD 64
#define USER_FILE "users.dat"
#define HISTORY_FILE "chat_history.dat"

static const uint8_t AES_KEY[AES_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

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

int server_socket;
ClientList *client_list;
int server_running = 1; // Flag to control server operation
AESContext aes_ctx;


void save_encrypted_message(const char *message, AESContext *ctx) {
    FILE *file = fopen(HISTORY_FILE, "a");
    if (file == NULL) {
        perror("Cannot open history file");
        return;
    }

    size_t len = strlen(message);
    size_t padded_len = ((len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
    uint8_t *padded_input = calloc(padded_len, 1);
    memcpy(padded_input, message, len);

    // PKCS#5/PKCS#7 padding
    uint8_t padding = padded_len - len;
    for (size_t i = len; i < padded_len; i++) {
        padded_input[i] = padding;
    }

    // Mã hóa tin nhắn
    uint8_t *encrypted = malloc(padded_len);
    aes_encrypt_ecb(ctx, padded_input, encrypted, padded_len);

    // Ghi độ dài tin nhắn gốc và dữ liệu mã hóa vào file
    fwrite(&len, sizeof(size_t), 1, file);  // Lưu độ dài gốc để giải mã
    fwrite(encrypted, 1, padded_len, file);

    free(padded_input);
    free(encrypted);
    fclose(file);
}

void save_file(int client_socket, const char *filename, size_t file_size) {
    char filepath[BUFFER_SIZE];
    snprintf(filepath, BUFFER_SIZE, "uploads/%s", filename);

    FILE *file = fopen(filepath, "wb");
    if (!file) {
        perror("Cannot create file");
        return;
    }

    char *buffer = malloc(FILE_BUFFER_SIZE);  // Dùng FILE_BUFFER_SIZE
    if (!buffer) {
        perror("Failed to allocate buffer");
        fclose(file);
        return;
    }

    size_t total_received = 0;
    int bytes_received;
    while (total_received < file_size &&
           (bytes_received = recv(client_socket, buffer, FILE_BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        total_received += bytes_received;
           }

    free(buffer);
    fclose(file);
    printf("File %s saved (%zu bytes)\n", filename, total_received);
}

void send_file(int client_socket, const char *filename) {
    char filepath[BUFFER_SIZE];
    snprintf(filepath, BUFFER_SIZE, "uploads/%s", filename);

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        send(client_socket, "FILE_NOT_FOUND", strlen("FILE_NOT_FOUND"), 0);
        return;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    send(client_socket, &file_size, sizeof(size_t), 0);

    char *buffer = malloc(FILE_BUFFER_SIZE);  // Dùng FILE_BUFFER_SIZE
    if (!buffer) {
        perror("Failed to allocate buffer");
        fclose(file);
        return;
    }

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, FILE_BUFFER_SIZE, file)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    free(buffer);
    fclose(file);
    printf("File %s sent to client\n", filename);
}

void send_chat_history(int client_socket, AESContext *ctx) {
    FILE *file = fopen(HISTORY_FILE, "r");
    if (file == NULL) {
        return;
    }

    size_t msg_len;
    printf("client_socket: %d\n", client_socket);
    while (fread(&msg_len, sizeof(size_t), 1, file) == 1) {
        size_t padded_len = ((msg_len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
        uint8_t *encrypted = malloc(padded_len);
        fread(encrypted, 1, padded_len, file);

        uint8_t *decrypted = malloc(padded_len);
        aes_decrypt_ecb(ctx, encrypted, decrypted, padded_len);

        uint8_t padding = decrypted[padded_len - 1];
        size_t unpadded_len = padded_len - padding;
        if (unpadded_len > msg_len) unpadded_len = msg_len;

        decrypted[unpadded_len] = '\0';
        char formatted_message[BUFFER_SIZE];
        snprintf(formatted_message, BUFFER_SIZE, "%s\n", (char *)decrypted);
        printf("%s", formatted_message);
        send(client_socket, formatted_message, strlen(formatted_message), 0);
        usleep(10000);
        free(encrypted);
        free(decrypted);
    }
    fclose(file);
}
// Mã hóa mật khẩu bằng AES-ECB
void encrypt_password(const char *password, char *encrypted) {

    size_t len = strlen(password);
    size_t padded_len = ((len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
    uint8_t *padded_input = calloc(padded_len, 1);
    memcpy(padded_input, password, len);

    // PKCS#5/PKCS#7 padding
    uint8_t padding = padded_len - len;
    for (size_t i = len; i < padded_len; i++) {
        padded_input[i] = padding;
    }

    // Mã hóa
    aes_encrypt_ecb(&aes_ctx, padded_input, (uint8_t *)encrypted, padded_len);

    free(padded_input);
}

// Xác thực mật khẩu
int validate_password(const char *password, const char *encrypted) {
    AESContext ctx;
    aes_init(&ctx, AES_KEY);

    size_t len = strlen(password);
    size_t padded_len = ((len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;

    // Giải mã
    uint8_t *decrypted = malloc(padded_len);
    aes_decrypt_ecb(&ctx, (const uint8_t *)encrypted, decrypted, padded_len);

    // So sánh với mật khẩu gốc
    int result = (strncmp((char *)decrypted, password, len) == 0);

    free(decrypted);
    return result;
}

// Đăng ký người dùng mới
int register_user(const char *username, const char *password) {
    User user;
    FILE *file = fopen(USER_FILE, "a+");

    if (file == NULL) {
        perror("Cannot open user file");
        return -1;
    }

    // Kiểm tra xem tên người dùng đã tồn tại chưa
    User temp_user;
    while (fread(&temp_user, sizeof(User), 1, file)) {
        if (strcmp(temp_user.username, username) == 0) {
            fclose(file);
            return 0; // Người dùng đã tồn tại
        }
    }

    // Tạo người dùng mới
    strcpy(user.username, username);
    encrypt_password(password, user.password);

    fwrite(&user, sizeof(User), 1, file);
    fclose(file);
    return 1; // Đăng ký thành công
}

// Đăng nhập người dùng
int login_user(const char *username, const char *password) {
    User user;
    FILE *file = fopen(USER_FILE, "r");

    if (file == NULL) {
        perror("Cannot open user file");
        return -1;
    }

    while (fread(&user, sizeof(User), 1, file)) {
        if (strcmp(user.username, username) == 0) {
            fclose(file);
            return validate_password(password, user.password);
        }
    }

    fclose(file);
    return 0; // Người dùng không tồn tại
}

void broadcast_message(const char *message, const char *sender) {
    pthread_mutex_lock(&client_list->clients_mutex);
    if (sender!=NULL) printf("%s\n", message);
    for (int i = 0; i < client_list->client_count; i++) {
        if (client_list->clients[i]->is_logged_in &&
            (sender == NULL || strcmp(client_list->clients[i]->username, sender) != 0)) {
            send(client_list->clients[i]->socket, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&client_list->clients_mutex);
}

void *handle_client(void *arg) {
    Client *client = arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;

    while ((bytes_received = recv(client->socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        char command[20];
        char param1[100];
        char param2[BUFFER_SIZE];

        sscanf(buffer, "%s %s %[^\n]", command, param1, param2);

        // Xử lý lệnh
        if (strcmp(command, "REGISTER") == 0) {
            char username[MAX_USERNAME], password[MAX_PASSWORD];
            sscanf(buffer, "REGISTER %s %s", username, password);

            int result = register_user(username, password);
            if (result == 1) {
                send(client->socket, "REGISTER_SUCCESS", strlen("REGISTER_SUCCESS"), 0);
            } else if (result == 0) {
                send(client->socket, "EXIST", strlen("EXIST"), 0);
            } else {
                send(client->socket, "REGISTER_FAIL", strlen("REGISTER_FAIL"),0);
            }
        } else if (strcmp(command, "LOGIN") == 0) {
            char username[MAX_USERNAME], password[MAX_PASSWORD];
            sscanf(buffer , "LOGIN %s %s", username, password);


            int result = login_user(username, password);
            if (result == 1) {
                strcpy(client->username, username);
                client->is_logged_in = 1;

                char welcome[BUFFER_SIZE];
                sprintf(welcome, "LOGIN_SUCCESS %s", username);
                printf("%s\n", welcome);
                send(client->socket, welcome, strlen(welcome), 0);

                // Thông báo cho tất cả mọi người
                char notification[BUFFER_SIZE];
                sprintf(notification, " %s đã tham gia cuộc trò chuyện \n", username);
                broadcast_message(notification, "Server");
                printf("Client logged in: %s with socket %d\n", client->username, client->socket);
                send_chat_history(client->socket, &aes_ctx);
            } else {
                send(client->socket, "LOGIN_FAIL", strlen("LOGIN_FAIL"), 0);
            }
        } else if (strcmp(buffer, "LOGOUT") == 0) {
            if (client->is_logged_in) {
                char notification[BUFFER_SIZE];
                sprintf(notification, " %s đã rời khỏi cuộc trò chuyện \n", client->username);
                broadcast_message(notification, "Server");

                client->is_logged_in = 0;
                send(client->socket, "LOGOUT_SUCCESS", strlen("LOGOUT_SUCCESS"), 0);
            } else {
                send(client->socket, "NOT_LOGGED_IN", strlen("NOT_LOGGED_IN"), 0);
            }
        } else if (strcmp(command, "UPLOAD") == 0) {
            if (!client->is_logged_in) {
                send(client->socket, "NOT_LOGGED_IN", strlen("NOT_LOGGED_IN"), 0);
                continue;
            }

            char filename[100];
            size_t file_size;
            sscanf(buffer, "UPLOAD %s %zu", filename, &file_size);

            save_file(client->socket, filename, file_size);

        } else if (strcmp(command, "DOWNLOAD") == 0) {
            if (!client->is_logged_in) {
                send(client->socket, "NOT_LOGGED_IN", strlen("NOT_LOGGED_IN"), 0);
                continue;
            }

            char filename[100];
            sscanf(buffer, "DOWNLOAD %s", filename);
            send_file(client->socket, filename);
        } else {
            if (client->is_logged_in) {
                broadcast_message(buffer, client->username);
                save_encrypted_message(buffer, &aes_ctx);
            } else {
                send(client->socket, "NOT_LOGGED_IN", strlen("NOT_LOGGED_IN"), 0);
            }
        }
    }

    // Người dùng ngắt kết nối
    close(client->socket);

    if (client->is_logged_in) {
        char notification[BUFFER_SIZE];
        sprintf(notification, " %s đã ngắt kết nối \n", client->username);
        broadcast_message(notification, "Server");
    }

    // Xóa client khỏi danh sách
    pthread_mutex_lock(&client_list->clients_mutex);
    for (int i = 0; i < client_list->client_count; i++) {
        if (client_list->clients[i] == client) {
            for (int j = i; j < client_list->client_count - 1; j++) {
                client_list->clients[j] = client_list->clients[j + 1];
            }
            client_list->client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&client_list->clients_mutex);

    free(client);
    return NULL;
}

// Thread để xử lý nhập liệu từ terminal server
void *handle_server_input(void *arg) {
    char buffer[BUFFER_SIZE];

    while (server_running) {
        // Đọc input từ terminal
        if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
            buffer[strcspn(buffer, "\n")] = 0;
            if (strcmp(buffer, "exit") == 0) {
                printf("Server shutting down...\n");
                server_running = 0;
                break;
            }
            char server_message[BUFFER_SIZE];
            sprintf(server_message, "MESSAGE: [Server] %s", buffer);

            broadcast_message(server_message, NULL);
        }
    }
    return NULL;
}



int main(int argc, char *argv[]) {
    int client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t tid, server_input_tid;
    aes_init(&aes_ctx, AES_KEY);
    // Khởi tạo danh sách client
    client_list = (ClientList *)malloc(sizeof(ClientList));
    client_list->client_count = 0;
    pthread_mutex_init(&client_list->clients_mutex, NULL);



    // Tạo socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Cấu hình địa chỉ server
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8888);

    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Socket binding failed");
        exit(EXIT_FAILURE);
    }

    // Lắng nghe kết nối
    if (listen(server_socket, 10) == -1) {
        perror("Socket listening failed");
        exit(EXIT_FAILURE);
    }

    printf("===== CHAT SERVER STARTED =====\n");
    printf("Waiting for clients on port %d...\n", ntohs(server_addr.sin_port));


    // Tạo thread để xử lý nhập liệu từ terminal server
    if (pthread_create(&server_input_tid, NULL, &handle_server_input, NULL) != 0) {
        perror("Failed to create server input thread");
        exit(EXIT_FAILURE);
    }

    // Chấp nhận và xử lý kết nối từ client
    while (server_running) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("Client connection failed");
            continue;
        }

        printf("New connection from %s:%d with socket %d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_socket);

        // Tạo client mới
        Client *client = malloc(sizeof(Client));
        client->socket = client_socket;
        client->is_logged_in = 0;
        strcpy(client->username, "");

        // Thêm client vào danh sách
        pthread_mutex_lock(&client_list->clients_mutex);
        if (client_list->client_count < MAX_CLIENTS) {
            client_list->clients[client_list->client_count++] = client;
            pthread_mutex_unlock(&client_list->clients_mutex);

            // Tạo thread mới để xử lý client
            if (pthread_create(&tid, NULL, &handle_client, client) != 0) {
                perror("Failed to create thread");
                pthread_mutex_lock(&client_list->clients_mutex);
                client_list->client_count--;
                pthread_mutex_unlock(&client_list->clients_mutex);
                free(client);
            }
        } else {
            pthread_mutex_unlock(&client_list->clients_mutex);
            printf("Max clients reached, connection rejected\n");
            close(client_socket);
            free(client);
        }
    }
    close(server_socket);
    return 0;
}