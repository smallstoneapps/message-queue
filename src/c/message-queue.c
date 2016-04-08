/*

MessageQueue Library for Pebble apps v2.0.0

----------------------

The MIT License (MIT)

Copyright © 2013 - 2015 Matthew Tole

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

--------------------

message-queue.c

*/

#include <pebble.h>
#include "message-queue.h"

#define KEY_GROUP 0
#define KEY_OPERATION 1
#define KEY_DATA 2

#define ATTEMPT_COUNT 2

typedef struct {
  char* group;
  char* operation;
  char* data;
} Message;

typedef struct MessageQueue MessageQueue;
struct MessageQueue {
  Message* message;
  MessageQueue* next;
  uint8_t attempts_left;
};

typedef struct HandlerQueue HandlerQueue;
struct HandlerQueue {
  MessageHandler handler;
  char* group;
  HandlerQueue* next;
};

static void destroy_message_queue(MessageQueue* queue);
static void outbox_sent_callback(DictionaryIterator *iterator, void *context);
static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context);
static void inbox_received_callback(DictionaryIterator *iterator, void *context);
static void send_next_message();
static char *translate_error(AppMessageResult result);

static MessageQueue* msg_queue = NULL;
static HandlerQueue* handler_queue = NULL;
static bool sending = false;
static bool can_send = false;
static bool s_autostart = false;

void mqueue_init(bool autostart) {
  AppMessageResult result = app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  if (APP_MSG_OK != result) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "INIT ERROR: %s", translate_error(result));
  }
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_inbox_received(inbox_received_callback);
  s_autostart = autostart;
}

bool mqueue_add(char* group, char* operation, char* data) {
  MessageQueue* mq = malloc(sizeof(MessageQueue));
  mq->next = NULL;
  mq->attempts_left = ATTEMPT_COUNT;

  mq->message = malloc(sizeof(Message));
  mq->message->group = malloc(strlen(group));
  strcpy(mq->message->group, group);
  mq->message->operation = malloc(strlen(operation));
  strcpy(mq->message->operation, operation);
  mq->message->data = malloc(strlen(data));
  strcpy(mq->message->data, data);

  APP_LOG(APP_LOG_LEVEL_DEBUG, "ADDING: %s, %s, %s", mq->message->group, mq->message->operation, mq->message->data);

  if (msg_queue == NULL) {
    msg_queue = mq;
  }
  else {
    MessageQueue* eoq = msg_queue;
    while (eoq->next != NULL) {
      eoq = eoq->next;
    }
    eoq->next = mq;
  }

  send_next_message();

  return true;
}

void mqueue_register_handler(char* group, MessageHandler handler) {
  HandlerQueue* hq = malloc(sizeof(HandlerQueue));
  hq->next = NULL;
  hq->group = malloc(strlen(group));
  strcpy(hq->group, group);
  hq->handler = handler;

  if (handler_queue == NULL) {
    handler_queue = hq;
  }
  else {
    HandlerQueue* eoq = handler_queue;
    while (eoq->next != NULL) {
      eoq = eoq->next;
    }
    eoq->next = hq;
  }
}

void mqueue_enable_sending(void) {
  can_send = true;
  send_next_message();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - //

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  sending = false;
  MessageQueue* sent = msg_queue;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "SENT: %s, %s, %s", sent->message->group, sent->message->operation, sent->message->data);
  msg_queue = msg_queue->next;
  destroy_message_queue(sent);
  send_next_message();
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  sending = false;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "ERROR: %s, %s, %s", msg_queue->message->group, msg_queue->message->operation, msg_queue->message->data);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", translate_error(reason));
  send_next_message();
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  char* group = dict_find(iterator, KEY_GROUP)->value->cstring;
  char* operation = dict_find(iterator, KEY_OPERATION)->value->cstring;
  char* data = dict_find(iterator, KEY_DATA)->value->cstring;

  HandlerQueue* hq = handler_queue;
  while (hq != NULL) {
    if (strcmp(group, hq->group) == 0) {
      hq->handler(operation, data);
    }
    hq = hq->next;
  }
  
  if (! can_send && s_autostart) {
    mqueue_enable_sending();
  }
}

static void destroy_message_queue(MessageQueue* queue) {
  free(queue->message->group);
  free(queue->message->operation);
  free(queue->message->data);
  free(queue->message);
  free(queue);
}

static void send_next_message() {
  if (! can_send) {
    return;
  }

  MessageQueue* mq = msg_queue;
  if (! mq) {
    return;
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "SENDING: %s, %s, %s", mq->message->group, mq->message->operation, mq->message->data);

  if (mq->attempts_left <= 0) {
    msg_queue = mq->next;
    destroy_message_queue(mq);
    send_next_message();
    return;
  }

  if (sending) {
    return;
  }
  sending = true;


  DictionaryIterator* dict;
  app_message_outbox_begin(&dict);
  dict_write_cstring(dict, KEY_GROUP, mq->message->group);
  dict_write_cstring(dict, KEY_DATA, mq->message->data);
  dict_write_cstring(dict, KEY_OPERATION, mq->message->operation);
  AppMessageResult result = app_message_outbox_send();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", translate_error(result));
  mq->attempts_left -= 1;
}

static char *translate_error(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "APP_MSG_OK";
    case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
    case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
    case APP_MSG_BUSY: return "APP_MSG_BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
    case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
    case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
    default: return "UNKNOWN ERROR";
  }
}
