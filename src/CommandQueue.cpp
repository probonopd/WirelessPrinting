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
  if (next == tail || command == "")
    return false;

  commandBuffer[head] = command;
  head = next;

  return true;
}

// Returns the next command to be sent, and advances to the next
String CommandQueue::popSend() {
  if (sendTail == head)
    return String();

  const String command = commandBuffer[sendTail];
  sendTail = nextBufferSlot(sendTail);
  
  return command;
}

// Returns the last command sent if it was received by the printer, otherwise returns empty
String CommandQueue::popAcknowledge() {
  if (isAckEmpty())
    return String();

  const String command = commandBuffer[tail];
  tail = nextBufferSlot(tail);

  return command;
}
