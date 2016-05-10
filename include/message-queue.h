#pragma once


#include <pebble.h>


typedef void (*MessageHandler)(char* operation, char* data);


void mqueue_init(bool autostart);
void mqueue_init_custom(bool autostart, uint16_t inbox_size, uint16_t outbox_size);
bool mqueue_add(char* group, char* operation, char* params);
void mqueue_register_handler(char* group, MessageHandler handler);
void mqueue_enable_sending(void);
