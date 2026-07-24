#include "server/timer_wheel.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    int fds[8];
    size_t count;
} expired_fds_t;

static void record_expired(int fd, void *context)
{
    expired_fds_t *expired = context;

    assert(expired->count < sizeof(expired->fds) / sizeof(expired->fds[0]));
    expired->fds[expired->count++] = fd;
}

static int publish_result(int fd, void *context)
{
    int *result = context;

    (void)fd;
    return *result;
}

static void test_expiry_and_refresh(void)
{
    timer_wheel_t *wheel = timer_wheel_create(3);
    struct timespec now;
    expired_fds_t expired = {0};

    assert(wheel != NULL);
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    now.tv_nsec = 200000000L;
    assert(timer_wheel_track_at(wheel, 10, &now) == 0);
    assert(timer_wheel_track_at(wheel, 11, &now) == 0);
    assert(timer_wheel_track_at(wheel, 10, &now) == -1);
    assert(timer_wheel_size(wheel) == 2);

    now.tv_sec += 2;
    assert(timer_wheel_touch_at(wheel, 10, &now) == 0);
    now.tv_sec += 2;
    assert(timer_wheel_advance_at(wheel, &now, record_expired, &expired) == 0);
    assert(expired.count == 1);
    assert(expired.fds[0] == 11);
    assert(timer_wheel_size(wheel) == 1);

    now.tv_sec += 2;
    assert(timer_wheel_advance_at(wheel, &now, record_expired, &expired) == 0);
    assert(expired.count == 2);
    assert(expired.fds[1] == 10);
    assert(timer_wheel_size(wheel) == 0);
    timer_wheel_destroy(wheel);
}

static void test_remove_and_fd_reuse(void)
{
    timer_wheel_t *wheel = timer_wheel_create(2);
    struct timespec now;
    expired_fds_t expired = {0};

    assert(wheel != NULL);
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    now.tv_nsec = 0;
    assert(timer_wheel_track_at(wheel, 20, &now) == 0);
    timer_wheel_remove(wheel, 20);
    assert(timer_wheel_size(wheel) == 0);

    now.tv_sec += 1;
    assert(timer_wheel_track_at(wheel, 20, &now) == 0);
    now.tv_sec += 1;
    assert(timer_wheel_advance_at(wheel, &now, record_expired, &expired) == 0);
    assert(expired.count == 0);
    now.tv_sec += 1;
    assert(timer_wheel_advance_at(wheel, &now, record_expired, &expired) == 0);
    assert(expired.count == 1);
    assert(expired.fds[0] == 20);
    timer_wheel_destroy(wheel);
}

static void test_publish_is_rolled_back_on_failure(void)
{
    timer_wheel_t *wheel = timer_wheel_create(2);
    int result = -1;

    assert(wheel != NULL);
    assert(timer_wheel_track_and_publish(wheel, 30, publish_result,
                                         &result) == -1);
    assert(timer_wheel_size(wheel) == 0);

    result = 0;
    assert(timer_wheel_track_and_publish(wheel, 30, publish_result,
                                         &result) == 0);
    assert(timer_wheel_size(wheel) == 1);
    timer_wheel_remove(wheel, 30);
    timer_wheel_destroy(wheel);
}

int main(void)
{
    test_expiry_and_refresh();
    test_remove_and_fd_reuse();
    test_publish_is_rolled_back_on_failure();
    puts("timer wheel tests passed");
    return 0;
}
