#include "CommandQueue.h"

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

bool CommandQueue::push(const String command) {
  int next = nextBufferSlot(head);
  if (next == tail || command == "")
    return false;

  buffer[head] = command;
  head = next;

  return true;
}

String CommandQueue::popSend() {
  if (sendTail == head)
    return "";

  const String command = buffer[sendTail];
  sendTail = nextBufferSlot(sendTail);
  
  return command;
}

String CommandQueue::popAcknowledge() {
  if (isAckEmpty())
    return "";

  const String command = buffer[tail];
  tail = nextBufferSlot(tail);

  return command;
}
