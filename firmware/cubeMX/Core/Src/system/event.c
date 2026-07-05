#include "system/event.h"

#define EVENT_QUEUE_SIZE 8

static Event_t event_queue[EVENT_QUEUE_SIZE];
static uint8_t head = 0;
static uint8_t tail = 0;
static uint8_t count = 0;

void event_push(Event_t event) {
  if (count >= EVENT_QUEUE_SIZE)
    return;
  event_queue[head] = event;
  head = (head + 1) % EVENT_QUEUE_SIZE;
  count++;
}

bool event_pop(Event_t *out) {
  if (count == 0)
    return false;
  *out = event_queue[tail];
  tail = (tail + 1) % EVENT_QUEUE_SIZE;
  count--;
  return true;
}

bool event_pending(void) { return count > 0; }
