#include <stdio.h>      // Standard I/O operations
#include <stdlib.h>     // Standard library for memory and process control
#include <string.h>     // String handling functions
#include <unistd.h>     // UNIX standard function definitions (read, write, etc.)
#include <fcntl.h>      // File control options
#include <termios.h>    // Terminal I/O interfaces for serial configuration
#include <arpa/inet.h>  // Internet address manipulation (for MQTT socket connection)

// Define constants for serial and MQTT configurations
#define SERIAL_PORT "/dev/ttyACM0"     // Serial port device file
#define BAUDRATE B115200               // Baud rate for serial communication
#define MQTT_BROKER "127.0.0.1"        // Localhost MQTT broker address
#define MQTT_PORT 1883                 // Default MQTT port
#define MQTT_TOPIC "stm32/data"        // Topic to publish data to

// Function to configure the serial port settings
int configure_serial(int serial_port) {
    struct termios tty;
    if (tcgetattr(serial_port, &tty) != 0) {  // Get current attributes of the serial port
        perror("Error getting serial attributes");
        return -1;
    }

    // Set input/output baud rate
    cfsetospeed(&tty, BAUDRATE);
    cfsetispeed(&tty, BAUDRATE);

    // Configure control flags: enable receiver, ignore modem control lines
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;      // No parity bit
    tty.c_cflag &= ~CSTOPB;      // One stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;          // 8 data bits
    tty.c_cflag &= ~CRTSCTS;     // Disable RTS/CTS hardware flow control

    // Configure local modes: disable canonical mode and echo
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    // Configure input modes: disable software flow control
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Configure output modes: disable post-processing
    tty.c_oflag &= ~OPOST;

    // Apply the settings
    tcsetattr(serial_port, TCSANOW, &tty);

    // Set the serial port to non-blocking mode
    fcntl(serial_port, F_SETFL, O_NONBLOCK);

    return 0;
}

// Function to create a simple MQTT PUBLISH packet
int create_mqtt_publish_packet(char *packet, const char *message) {
    int pos = 0;
    int topic_len = strlen(MQTT_TOPIC);
    int msg_len = strlen(message);

    packet[pos++] = 0x30;                         // MQTT PUBLISH command
    packet[pos++] = 2 + topic_len + msg_len;      // Remaining length
    packet[pos++] = 0x00;                         // Topic name MSB
    packet[pos++] = topic_len;                    // Topic name LSB
    memcpy(&packet[pos], MQTT_TOPIC, topic_len);  // Copy topic name
    pos += topic_len;
    memcpy(&packet[pos], message, msg_len);       // Copy message payload
    pos += msg_len;

    return pos; // Return total packet size
}

int main() {
    // Open the serial port
    int serial_port = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NDELAY);
    if (serial_port == -1) {
        perror("Error opening serial port");
        return 1;
    }

    // Configure the serial port
    if (configure_serial(serial_port) != 0) {
        close(serial_port);
        return 1;
    }

    // Create a TCP socket for MQTT communication
    int mqtt_sock;
    struct sockaddr_in server_addr;
    char packet[256];

    mqtt_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (mqtt_sock == -1) {
        perror("Socket creation failed");
        return 1;
    }

    // Define broker address and port
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(MQTT_PORT);
    server_addr.sin_addr.s_addr = inet_addr(MQTT_BROKER);

    // Connect to the MQTT broker
    if (connect(mqtt_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("MQTT connection failed");
        close(mqtt_sock);
        return 1;
    }

    // Set receive timeout for MQTT socket
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(mqtt_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    // Send MQTT CONNECT packet
    packet[0] = 0x10; // CONNECT control packet type
    packet[1] = 14;   // Remaining length
    memcpy(&packet[2], "\x00\x04MQTT\x04\x02\x00\x3C\x00\x00", 12); // Protocol name, level, flags, keep-alive, client ID length
    send(mqtt_sock, packet, 14, 0);
    recv(mqtt_sock, packet, sizeof(packet), 0); // Receive CONNACK

    printf("Connected to MQTT broker\n");

    // Buffer to read serial data
    char buffer[256];
    int buffer_pos = 0;

    // Continuous loop to read from serial and send to MQTT
    while (1) {
        char ch;
        int bytes_read = read(serial_port, &ch, 1); // Read one byte at a time

        if (bytes_read > 0) {
            if (ch == '\n' || buffer_pos >= sizeof(buffer) - 1) {
                buffer[buffer_pos] = '\0'; // Null-terminate the string
                if (buffer_pos > 0) {
                    printf("Received: %s\n", buffer);
                    int len = create_mqtt_publish_packet(packet, buffer);
                    send(mqtt_sock, packet, len, 0); // Send to MQTT broker
                }
                buffer_pos = 0; // Reset buffer
            } else {
                buffer[buffer_pos++] = ch; // Accumulate character in buffer
            }
        }

        usleep(20000); // Sleep for 20 milliseconds to reduce CPU usage
    }

    // Close sockets and serial port
    close(mqtt_sock);
    close(serial_port);
    return 0;
}
