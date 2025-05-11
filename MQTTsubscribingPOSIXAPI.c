#include <stdio.h>       // Standard I/O functions
#include <stdlib.h>      // General utilities
#include <string.h>      // String manipulation functions
#include <unistd.h>      // POSIX API for close(), usleep(), etc.
#include <arpa/inet.h>   // Functions for network address manipulation

// Define MQTT connection parameters
#define MQTT_BROKER "127.0.0.1"       // IP address of MQTT broker (localhost)
#define MQTT_PORT 1883                // Default MQTT port
#define MQTT_TOPIC "stm32/data"       // Topic to subscribe to

// Function to create an MQTT CONNECT packet
int create_mqtt_connect_packet(char *packet) {
    int pos = 0;
    packet[pos++] = 0x10;            // MQTT Control Packet type (CONNECT)
    packet[pos++] = 14;              // Remaining length

    // Protocol Name: "MQTT"
    packet[pos++] = 0x00; packet[pos++] = 0x04;  // Length of "MQTT"
    memcpy(&packet[pos], "MQTT", 4);
    pos += 4;

    packet[pos++] = 0x04;            // Protocol level (4 for MQTT 3.1.1)
    packet[pos++] = 0x02;            // Connect flags (Clean Session)

    packet[pos++] = 0x00; packet[pos++] = 0x3C;  // Keep Alive (60 seconds)

    packet[pos++] = 0x00; packet[pos++] = 0x00;  // Client ID length = 0 (no client ID)

    return pos;                      // Return total bytes in packet
}

// Function to create an MQTT SUBSCRIBE packet
int create_mqtt_subscribe_packet(char *packet) {
    int pos = 0;
    int topic_len = strlen(MQTT_TOPIC);

    packet[pos++] = 0x82;            // MQTT Control Packet type (SUBSCRIBE)
    packet[pos++] = 2 + topic_len + 1; // Remaining length

    packet[pos++] = 0x00; packet[pos++] = 0x01;  // Packet Identifier

    // Topic filter
    packet[pos++] = (topic_len >> 8) & 0xFF;     // MSB of topic length
    packet[pos++] = topic_len & 0xFF;            // LSB of topic length
    memcpy(&packet[pos], MQTT_TOPIC, topic_len); // Topic string
    pos += topic_len;

    packet[pos++] = 0x00;            // QoS level 0

    return pos;                      // Return total bytes in packet
}

int main() {
    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        return 1;
    }

    // Set up MQTT broker address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(MQTT_PORT);
    server_addr.sin_addr.s_addr = inet_addr(MQTT_BROKER);

    // Connect to MQTT broker
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("MQTT connection failed");
        close(sock);
        return 1;
    }

    // Set receive timeout for socket
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    char packet[256];

    // Send MQTT CONNECT packet
    send(sock, packet, create_mqtt_connect_packet(packet), 0);
    recv(sock, packet, sizeof(packet), 0); // Wait for CONNACK

    printf("Connected to MQTT broker\n");

    // Send MQTT SUBSCRIBE packet
    send(sock, packet, create_mqtt_subscribe_packet(packet), 0);
    recv(sock, packet, sizeof(packet), 0); // Wait for SUBACK

    printf("Subscribed to topic: %s\n", MQTT_TOPIC);

    // Loop to receive and print published messages
    while (1) {
        memset(packet, 0, sizeof(packet));
        int bytes_received = recv(sock, packet, sizeof(packet), 0);
        if (bytes_received > 0) {
            // Parse and print the message payload
            int topic_length = (packet[2] << 8) | packet[3];
            char *message = packet + 4 + topic_length;
            printf("Received: %s\n", message);
        }
        usleep(100000); // Sleep for 100ms
    }

    close(sock); // Close socket connection
    return 0;
}
