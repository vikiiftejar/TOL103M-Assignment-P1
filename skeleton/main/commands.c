// CSTDLIB includes
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

// ESP32 SDK includes
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <errno.h>

// Custom includes
#include "commands.h"
#include "serial_io.h"

#define MSG_BUFFER_LENGTH 128

enum CommandToken {
    COMMAND_UNKNOWN,
    COMMAND_MAC,
    COMMAND_ID,
    COMMAND_STATUS,
    COMMAND_DEC
};

typedef struct Command {
    enum CommandToken key;
    char command[MSG_BUFFER_LENGTH];
    char argument[MSG_BUFFER_LENGTH];
} Command;

void init_Command(Command * cmd) {
    if (!cmd) return;
    cmd->key = COMMAND_UNKNOWN;
    memset(cmd->command, 0, MSG_BUFFER_LENGTH);
    memset(cmd->argument, 0, MSG_BUFFER_LENGTH);
}

// Forward declarations
int parse_input(const char* in_msg, Command* out_cmd);
int process_cmd_mac(const Command* in_cmd, char* out_msg);
int process_cmd_id(const Command* in_cmd, char* out_msg);
int process_cmd_status(const Command* in_cmd, char* out_msg);
int process_cmd_dec(const Command* in_cmd, char* out_msg);

// Command dispatcher
int process_command(const char* in_msg, char* out_msg) {
    if (!in_msg || !out_msg) return -1;

    char str_buffer[MSG_BUFFER_LENGTH];
    memset(str_buffer, 0, MSG_BUFFER_LENGTH);

    Command cmd;
    init_Command(&cmd);
    parse_input(in_msg, &cmd);

    int result = 0;
    switch (cmd.key) {
        case COMMAND_MAC:
            result = process_cmd_mac(&cmd, str_buffer);
            break;
        case COMMAND_ID:
            result = process_cmd_id(&cmd, str_buffer);
            break;
        case COMMAND_STATUS:
            result = process_cmd_status(&cmd, str_buffer);
            break;
        case COMMAND_DEC:
            result = process_cmd_dec(&cmd, str_buffer);
            break;
        default:
            strncpy(str_buffer, "COMMAND ERROR\n", MSG_BUFFER_LENGTH-1);
            str_buffer[MSG_BUFFER_LENGTH-1] = '\0';
            result = 1;
    }

    // Add newline at end
    size_t len = strlen(str_buffer);
    if (len < MSG_BUFFER_LENGTH - 1 && str_buffer[len-1] != '\n') {
        str_buffer[len] = '\n';
        str_buffer[len+1] = '\0';
    }

    strncpy(out_msg, str_buffer, MSG_BUFFER_LENGTH);
    out_msg[MSG_BUFFER_LENGTH-1] = '\0';
    return result;
}

// Parse input into command + argument (split at first space)
int parse_input(const char* in_msg, Command* out_cmd) {
    if (!in_msg || !out_cmd) return -1;

    init_Command(out_cmd);

    // Skip leading spaces
    size_t i = 0;
    while (in_msg[i] == ' ' || in_msg[i] == '\t') i++;

    // Copy command until first space or end
    size_t j = 0;
    while (in_msg[i] != '\0' && in_msg[i] != ' ' && in_msg[i] != '\t' && j < MSG_BUFFER_LENGTH-1) {
        out_cmd->command[j++] = (char)tolower(in_msg[i++]);
    }
    out_cmd->command[j] = '\0';

    // Skip spaces between command and argument
    while (in_msg[i] == ' ' || in_msg[i] == '\t') i++;

    // Copy remaining string as argument
    size_t k = 0;
    while (in_msg[i] != '\0' && k < MSG_BUFFER_LENGTH-1) {
        out_cmd->argument[k++] = in_msg[i++];
    }
    out_cmd->argument[k] = '\0';

    if (!strcmp(out_cmd->command, "mac")) out_cmd->key = COMMAND_MAC;
    else if (!strcmp(out_cmd->command, "id")) out_cmd->key = COMMAND_ID;
    else if (!strcmp(out_cmd->command, "status")) out_cmd->key = COMMAND_STATUS;
    else if (!strcmp(out_cmd->command, "dec")) out_cmd->key = COMMAND_DEC;
    else out_cmd->key = COMMAND_UNKNOWN;

    return 0;
}

// MAC command
int process_cmd_mac(const Command* in_cmd, char* out_msg) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out_msg, MSG_BUFFER_LENGTH,
             "MAC %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

// ID command
int process_cmd_id(const Command* in_cmd, char* out_msg) {
    const char* email_list = "imv2@hi.is,hfa8@hi.is";
    strncpy(out_msg, email_list, MSG_BUFFER_LENGTH-1);
    out_msg[MSG_BUFFER_LENGTH-1] = '\0';
    return 0;
}

// STATUS command
int process_cmd_status(const Command* in_cmd, char* out_msg) {
    int64_t uptime_us = esp_timer_get_time();
    int uptime_s = (int)(uptime_us / 1000000);

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    size_t free_heap = esp_get_free_heap_size();

    snprintf(out_msg, MSG_BUFFER_LENGTH,
             "SYSTEM UPTIME: %d S\n"
             "AVAILABLE CORES: %d\n"
             "AVAILABLE HEAP MEMORY: %zu\n",
             uptime_s, chip_info.cores, free_heap);
    return 0;
}

// DEC command
int process_cmd_dec(const Command* in_cmd, char* out_msg) {
    if (strlen(in_cmd->argument) == 0) {
        strcpy(out_msg, "ARGUMENT ERROR");
        return 2;
    }

    char* end_ptr;
    unsigned long value = 0;

    if (strncmp(in_cmd->argument, "0b", 2) == 0 || strncmp(in_cmd->argument, "0B", 2) == 0) {
        value = strtoul(in_cmd->argument+2, &end_ptr, 2);
    } else if (strncmp(in_cmd->argument, "0x", 2) == 0 || strncmp(in_cmd->argument, "0X", 2) == 0) {
        value = strtoul(in_cmd->argument+2, &end_ptr, 16);
    } else if (in_cmd->argument[0] == '0' && isdigit((unsigned char)in_cmd->argument[1])) {
        value = strtoul(in_cmd->argument, &end_ptr, 8);
    } else {
        value = strtoul(in_cmd->argument, &end_ptr, 10);
    }

    if (*end_ptr != '\0' || value > 65535) {
        strcpy(out_msg, "ARGUMENT ERROR");
        return 2;
    }

    snprintf(out_msg, MSG_BUFFER_LENGTH, "%lu", value);
    return 0;
}

