#pragma once

#define COMMAND_BUFFER_SIZE   16

#include <Arduino.h>

class CommandQueue {
  private:
    static int head, sendTail, tail;
    static String buffer[COMMAND_BUFFER_SIZE];

    static inline int nextBufferSlot(int index) {
      int next = index + 1;
  
      return next >= COMMAND_BUFFER_SIZE ? 0 : next;
    }

  public:
    static inline bool isEmpty() {
      return head == tail;
    }

    static inline bool isAckEmpty() {
      return tail == sendTail;
    }

    static int getFreeSlots();

    static inline void clear() {
      head = sendTail = tail;
    }

    static bool push(const String command);

    inline static String peekSend() {
      return (sendTail == head) ? "" : buffer[sendTail];
    }

    static String popSend();
    static String popAcknowledge();
};

extern CommandQueue commandQueue;
