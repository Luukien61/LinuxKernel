#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 2048
#define MAX_USERNAME 50
#define MAX_PASSWORD 50
#define USER_FILE "users.dat"

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

ClientList *client_list;

// Tạo một salt ngẫu nhiên cho mã hóa mật khẩu
void create_salt(char *salt, int length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    srand(time(NULL));

    for (int i = 0; i < length; i++) {
        int key = rand() % (int)(sizeof(charset) - 1);
        salt[i] = charset[key];
    }
    salt[length] = '\0';
}

// Mã hóa mật khẩu đơn giản
void encrypt_password(const char *password, char *encrypted) {
    char salt[9];
    create_salt(salt, 8);

    strcpy(encrypted, salt);

    for (int i = 0; i < strlen(password); i++) {
        encrypted[i + 8] = password[i] + salt[i % 8] % 26;
    }
    encrypted[strlen(password) + 8] = '\0';
}

// Xác thực mật khẩu
int validate_password(const char *password, const char *encrypted) {
    char salt[9];
    strncpy(salt, encrypted, 8);
    salt[8] = '\0';

    for (int i = 0; i < strlen(password); i++) {
        if (encrypted[i + 8] != password[i] + salt[i % 8] % 26) {
            return 0;
        }
    }
    return 1;
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
    for (int i = 0; i < client_list->client_count; i++) {
        if (client_list->clients[i]->is_logged_in &&
            strcmp(client_list->clients[i]->username, sender) != 0) {
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
                sprintf(notification, "*** %s đã tham gia cuộc trò chuyện ***\n", username);
                broadcast_message(notification, "Server");
                printf("Client logged in: %s with socket %d\n", client->username, client->socket);
            } else {
                send(client->socket, "LOGIN_FAIL", strlen("LOGIN_FAIL"), 0);
            }
        } else if (strcmp(buffer, "LOGOUT") == 0) {
            if (client->is_logged_in) {
                char notification[BUFFER_SIZE];
                sprintf(notification, "*** %s đã rời khỏi cuộc trò chuyện ***\n", client->username);
                broadcast_message(notification, "Server");

                client->is_logged_in = 0;
                send(client->socket, "LOGOUT_SUCCESS", strlen("LOGOUT_SUCCESS"), 0);
            } else {
                send(client->socket, "NOT_LOGGED_IN", strlen("NOT_LOGGED_IN"), 0);
            }
        } else {
            if (client->is_logged_in) {
                broadcast_message(buffer, client->username);
            } else {
                send(client->socket, "NOT_LOGGED_IN", strlen("NOT_LOGGED_IN"), 0);
            }
        }
    }

    // Người dùng ngắt kết nối
    close(client->socket);

    if (client->is_logged_in) {
        char notification[BUFFER_SIZE];
        sprintf(notification, "*** %s đã ngắt kết nối ***\n", client->username);
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

int main(int argc, char *argv[]) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t tid;

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

    // Chấp nhận và xử lý kết nối từ client
    while (1) {
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
}