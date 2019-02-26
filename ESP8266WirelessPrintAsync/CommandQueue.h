#pragma once

#define COMMAND_BUFFER_SIZE   16

#include <Arduino.h>

class CommandQueue {
  private:
    static int head, sendTail, tail;
    static String commandBuffer[COMMAND_BUFFER_SIZE];

    // Returns the next buffer slot (after index slot) if it's in between the size of the buffer
    static inline int nextBufferSlot(int index) {
      int next = index + 1;
  
      return next >= COMMAND_BUFFER_SIZE ? 0 : next;
    }

  public:
    // Check if buffer is empty
    static inline bool isEmpty() {
      return head == tail;
    }

    // Returns true if the command to be sent was the last sent (so there is no pending response)
    static inline bool isAckEmpty() {
      return tail == sendTail;
    }

    static int getFreeSlots();

    static inline void clear() {
      head = sendTail = tail;
    }

    static bool push(const String command);

    // If there is a command pending to be sent returns it
    inline static String peekSend() {
      return (sendTail == head) ? String() : commandBuffer[sendTail];
    }
    
    static String popSend();
    static String popAcknowledge();
};

extern CommandQueue commandQueue;
