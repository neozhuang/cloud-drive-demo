#pragma once

#include <stddef.h>
#include <time.h>

typedef struct timer_wheel timer_wheel_t;
typedef void (*timer_wheel_expire_fn)(int fd, void *context);
typedef int (*timer_wheel_publish_fn)(int fd, void *context);

timer_wheel_t *timer_wheel_create(unsigned int timeout_seconds);
void timer_wheel_destroy(timer_wheel_t *wheel);

int timer_wheel_track(timer_wheel_t *wheel, int fd);
int timer_wheel_track_and_publish(timer_wheel_t *wheel, int fd,
                                  timer_wheel_publish_fn publish,
                                  void *context);
int timer_wheel_touch(timer_wheel_t *wheel, int fd);
void timer_wheel_remove(timer_wheel_t *wheel, int fd);
int timer_wheel_advance(timer_wheel_t *wheel,
                        timer_wheel_expire_fn expire,
                        void *context);

/* Explicit-time variants keep deadline behavior deterministic in tests. */
int timer_wheel_track_at(timer_wheel_t *wheel, int fd,
                         const struct timespec *now);
int timer_wheel_touch_at(timer_wheel_t *wheel, int fd,
                         const struct timespec *now);
int timer_wheel_advance_at(timer_wheel_t *wheel,
                           const struct timespec *now,
                           timer_wheel_expire_fn expire,
                           void *context);
size_t timer_wheel_size(timer_wheel_t *wheel);
