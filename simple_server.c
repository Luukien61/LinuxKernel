#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define PORT 8888

typedef struct {
    int socket;
    char username[50];
    struct sockaddr_in address;
} Client;

typedef struct {
    Client clients[MAX_CLIENTS];
    int count;
    pthread_mutex_t mutex;
} ClientList;

ClientList client_list;

// Function to broadcast a message to all clients except the sender
void broadcast_message(const char *message, int sender_socket) {
    pthread_mutex_lock(&client_list.mutex);

    for (int i = 0; i < client_list.count; i++) {
        if (client_list.clients[i].socket != sender_socket) {
            if (send(client_list.clients[i].socket, message, strlen(message), 0) < 0) {
                perror("Send failed");
            }
        }
    }

    pthread_mutex_unlock(&client_list.mutex);
}

// Function to handle each client connection
void *handle_client(void *arg) {
    Client *client = (Client*)arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;

    // Ask for username
    if (send(client->socket, "Please enter your username:", 27, 0) < 0) {
        perror("Send failed");
        close(client->socket);
        return NULL;
    }

    // Get username
    bytes_received = recv(client->socket, buffer, BUFFER_SIZE, 0);
    if (bytes_received <= 0) {
        perror("Username receive failed");
        close(client->socket);
        return NULL;
    }

    buffer[bytes_received] = '\0';
    strncpy(client->username, buffer, 49);
    client->username[49] = '\0'; // Ensure null-terminated

    printf("Client %s connected with socket %d from %s:%d\n",
           client->username,
           client->socket,
           inet_ntoa(client->address.sin_addr),
           ntohs(client->address.sin_port));

    // Notify all clients about new connection
    char welcome_msg[BUFFER_SIZE];
    sprintf(welcome_msg, "--- %s has joined the chat ---\n", client->username);
    broadcast_message(welcome_msg, -1); // -1 means broadcast to all

    // Main message loop
    while ((bytes_received = recv(client->socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';

        // Check for exit command
        if (strcmp(buffer, "exit") == 0) {
            break;
        }

        // Format and broadcast the message
        char formatted_msg[BUFFER_SIZE + 50];
        sprintf(formatted_msg, "%s: %s", client->username, buffer);
        broadcast_message(formatted_msg, client->socket);
    }

    // Clean up when client disconnects
    printf("Client %s disconnected from socket %d\n", client->username, client->socket);

    pthread_mutex_lock(&client_list.mutex);

    // Find the client in the list and remove it
    for (int i = 0; i < client_list.count; i++) {
        if (client_list.clients[i].socket == client->socket) {
            // Move all clients after this one back by one position
            for (int j = i; j < client_list.count - 1; j++) {
                client_list.clients[j] = client_list.clients[j + 1];
            }
            client_list.count--;
            break;
        }
    }

    pthread_mutex_unlock(&client_list.mutex);

    close(client->socket);

    // Notify everyone about the disconnect
    char leave_msg[BUFFER_SIZE];
    sprintf(leave_msg, "--- %s has left the chat ---\n", client->username);
    broadcast_message(leave_msg, -1);

    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;

    // Initialize client list
    client_list.count = 0;
    pthread_mutex_init(&client_list.mutex, NULL);

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Prepare the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind the socket to specified address and port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started. Listening on port %d...\n", PORT);

    // Accept and handle client connections
    while (1) {
        // Accept a connection
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        printf("New connection from %s:%d with socket %d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               client_socket);

        // Add the client to the list
        pthread_mutex_lock(&client_list.mutex);

        if (client_list.count >= MAX_CLIENTS) {
            pthread_mutex_unlock(&client_list.mutex);
            printf("Max clients reached. Connection rejected.\n");
            close(client_socket);
            continue;
        }

        Client *new_client = &client_list.clients[client_list.count];
        new_client->socket = client_socket;
        new_client->address = client_addr;
        strcpy(new_client->username, "Unknown"); // Will be updated once user provides a name

        client_list.count++;
        pthread_mutex_unlock(&client_list.mutex);

        // Create a thread to handle the client
        if (pthread_create(&thread_id, NULL, handle_client, (void *)new_client) != 0) {
            perror("Failed to create thread");
            pthread_mutex_lock(&client_list.mutex);
            client_list.count--;
            pthread_mutex_unlock(&client_list.mutex);
            close(client_socket);
            continue;
        }

        // Detach the thread - resources will be freed automatically when the thread terminates
        pthread_detach(thread_id);
    }

    // Clean up (this part will not be reached in this simple example)
    close(server_socket);
    pthread_mutex_destroy(&client_list.mutex);

    return 0;
}