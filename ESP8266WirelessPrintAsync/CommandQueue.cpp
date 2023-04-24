#include "CommandQueue.h"

CommandQueue commandQueue;    //FIFO Queue

int CommandQueue::head = 0;
int CommandQueue::sendTail = 0;
int CommandQueue::tail = 0;
String CommandQueue::commandBuffer[COMMAND_BUFFER_SIZE];

int CommandQueue::getFreeSlots() {
  int freeSlots = COMMAND_BUFFER_SIZE - 1;

  int next = tail;
  while (next != head) {
    --freeSlots;
    next = nextBufferSlot(next);
  }

  return freeSlots;
}

// Tries to Add a command to the queue, returns true if possible
bool CommandQueue::push(const String command) {
  int next = nextBufferSlot(head);
  if (next == tail || command == "") {
    log_w("Ignoring command; empty cmd or full buffer");
    return false;
  }

  commandBuffer[head] = command;
  head = next;
  //log_i("PUSH %s", command.c_str());

  return true;
}

// Returns the next command to be sent, and advances to the next
String CommandQueue::popSend() {
  if (sendTail == head)
    return String();

  const String command = commandBuffer[sendTail];
  sendTail = nextBufferSlot(sendTail);
  //log_i("POPSEND %s", command.c_str());

  return command;
}

// Returns the last command sent if it was received by the printer, otherwise returns empty
String CommandQueue::popAcknowledge() {
  if (isAckEmpty())
    return String();

  const String command = commandBuffer[tail];
  tail = nextBufferSlot(tail);
  //log_i("ACK %s", command.c_str());

  return command;
}
