#include "CommandQueue.h"
//FIFO Queue
CommandQueue commandQueue;

int CommandQueue::head = 0;
int CommandQueue::sendTail = 0;
int CommandQueue::tail = 0;
String CommandQueue::buffer[COMMAND_BUFFER_SIZE];

int CommandQueue::getFreeSlots() {
  int freeSlots = COMMAND_BUFFER_SIZE - 1;

  int next = tail;
  while (next != head) {
    --freeSlots;
    next = nextBufferSlot(next);
  }

  return freeSlots;
}

bool CommandQueue::push(const String command) { // Tries to Add a command to the queue, returns true if possible
  int next = nextBufferSlot(head);
  if (next == tail || command == "")
    return false;

  buffer[head] = command;
  head = next;

  return true;
}

String CommandQueue::popSend() {      // Returns the next command to be sent, and advances to the next
  if (sendTail == head)
    return "";

  const String command = buffer[sendTail];
  sendTail = nextBufferSlot(sendTail);
  
  return command;
}

String CommandQueue::popAcknowledge() { // Returns the last command sent if it was received by the printer, otherwise returns empty
  if (isAckEmpty())
    return "";

  const String command = buffer[tail];
  tail = nextBufferSlot(tail);

  return command;
}
