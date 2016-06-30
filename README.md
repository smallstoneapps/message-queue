# Message Queue [![npm (scoped)](https://img.shields.io/npm/v/@smallstoneapps/message-queue.svg?maxAge=2592000&style=flat-square)](https://www.npmjs.com/package/@smallstoneapps/message-queue)&nbsp;[![MIT License](http://img.shields.io/badge/license-MIT-lightgray.svg?style=flat-square)](./LICENSE)

A Pebble package that handles queuing of outgoing messages and allows for easy group based routing of incoming messages.

## Installation

*You must be using Pebble SDK 3.12 or newer to use this library.*

To install the package to your app, use the pebble tool:

```
pebble package install @smallstoneapps/message-queue
```

## Usage

### Setup the message queue

    mqueue_init();

### Add a message to be sent.

    mqueue_add("GROUP", "OPERATION", "DATA");

### Register a callback function for incoming messages of a particular group.

    mqueue_register_handler("GROUP", msg_handler);
