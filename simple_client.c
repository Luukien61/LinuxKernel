#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <gtk/gtk.h>

#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"
#define PORT 8888

int sock = 0;
char current_username[50];
int logged_in = 0;


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

// Function to receive messages from the server
void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    int bytes_received;

    while ((bytes_received = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("%s\n", buffer);
    }

    if (bytes_received == 0) {
        printf("Server disconnected.\n");
    } else {
        perror("recv failed");
    }

    close(sock);
    exit(0);

    return NULL;
}

int main() {
    struct sockaddr_in serv_addr;
    pthread_t recv_thread;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error\n");
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));

    // Set up server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address/ Address not supported\n");
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection Failed\n");
        return -1;
    }

    printf("Connected to server at %s:%d\n", SERVER_IP, PORT);
    printf("Your socket: %d\n", sock);

    // Create thread to receive messages
    if (pthread_create(&recv_thread, NULL, receive_messages, NULL) != 0) {
        printf("Thread creation failed\n");
        close(sock);
        return -1;
    }

    // Main loop to send messages
    char buffer[BUFFER_SIZE];

    // First message will be the username
    printf("Enter your username: ");
    fgets(buffer, BUFFER_SIZE, stdin);
    buffer[strcspn(buffer, "\n")] = 0; // Remove newline character

    if (send(sock, buffer, strlen(buffer), 0) < 0) {
        printf("Failed to send username\n");
        close(sock);
        return -1;
    }

    printf("Chat started. Type 'exit' to quit.\n");

    // Continue sending messages
    while (1) {
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline character

        if (send(sock, buffer, strlen(buffer), 0) < 0) {
            printf("Message send failed\n");
            break;
        }

        // Check if user wants to exit
        if (strcmp(buffer, "exit") == 0) {
            break;
        }
    }

    // Close the socket and terminate
    close(sock);

    return 0;
}