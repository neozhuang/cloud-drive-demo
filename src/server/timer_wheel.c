#include "server/timer_wheel.h"

#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>

#define TIMER_FD_BUCKET_COUNT 1024U

typedef struct timer_node timer_node_t;

struct timer_node {
    int fd;
    struct timespec deadline;
    size_t slot;
    timer_node_t *slot_previous;
    timer_node_t *slot_next;
    timer_node_t *hash_next;
};

struct timer_wheel {
    unsigned int timeout_seconds;
    size_t slot_count;
    time_t current_second;
    timer_node_t **slots;
    timer_node_t **fd_buckets;
    size_t size;
    pthread_mutex_t lock;
};

static size_t hash_fd(int fd)
{
    return (size_t)(unsigned int)fd % TIMER_FD_BUCKET_COUNT;
}

static int timespec_compare(const struct timespec *left,
                            const struct timespec *right)
{
    if (left->tv_sec != right->tv_sec) {
        return left->tv_sec < right->tv_sec ? -1 : 1;
    }
    if (left->tv_nsec != right->tv_nsec) {
        return left->tv_nsec < right->tv_nsec ? -1 : 1;
    }
    return 0;
}

static time_t deadline_tick(const struct timespec *deadline)
{
    return deadline->tv_sec + (deadline->tv_nsec != 0 ? 1 : 0);
}

static timer_node_t *find_node(timer_wheel_t *wheel, int fd)
{
    timer_node_t *node = wheel->fd_buckets[hash_fd(fd)];

    while (node != NULL) {
        if (node->fd == fd) {
            return node;
        }
        node = node->hash_next;
    }
    return NULL;
}

static void unlink_slot(timer_wheel_t *wheel, timer_node_t *node)
{
    if (node->slot_previous != NULL) {
        node->slot_previous->slot_next = node->slot_next;
    } else {
        wheel->slots[node->slot] = node->slot_next;
    }
    if (node->slot_next != NULL) {
        node->slot_next->slot_previous = node->slot_previous;
    }
    node->slot_previous = NULL;
    node->slot_next = NULL;
}

static void link_slot(timer_wheel_t *wheel, timer_node_t *node)
{
    time_t tick = deadline_tick(&node->deadline);

    node->slot = (size_t)((uint64_t)tick % wheel->slot_count);
    node->slot_next = wheel->slots[node->slot];
    if (node->slot_next != NULL) {
        node->slot_next->slot_previous = node;
    }
    wheel->slots[node->slot] = node;
}

static void unlink_hash(timer_wheel_t *wheel, timer_node_t *node)
{
    size_t bucket = hash_fd(node->fd);
    timer_node_t **link = &wheel->fd_buckets[bucket];

    while (*link != NULL && *link != node) {
        link = &(*link)->hash_next;
    }
    if (*link == node) {
        *link = node->hash_next;
    }
}

static int current_time(struct timespec *now)
{
    return clock_gettime(CLOCK_MONOTONIC, now);
}

timer_wheel_t *timer_wheel_create(unsigned int timeout_seconds)
{
    timer_wheel_t *wheel;
    struct timespec now;

    if (timeout_seconds == 0 || current_time(&now) != 0) {
        return NULL;
    }

    wheel = calloc(1, sizeof(*wheel));
    if (wheel == NULL) {
        return NULL;
    }
    wheel->timeout_seconds = timeout_seconds;
    wheel->slot_count = (size_t)timeout_seconds + 1U;
    wheel->current_second = now.tv_sec;
    wheel->slots = calloc(wheel->slot_count, sizeof(*wheel->slots));
    wheel->fd_buckets = calloc(TIMER_FD_BUCKET_COUNT,
                               sizeof(*wheel->fd_buckets));
    if (wheel->slots == NULL || wheel->fd_buckets == NULL) {
        free(wheel->fd_buckets);
        free(wheel->slots);
        free(wheel);
        return NULL;
    }
    if (pthread_mutex_init(&wheel->lock, NULL) != 0) {
        free(wheel->fd_buckets);
        free(wheel->slots);
        free(wheel);
        return NULL;
    }
    return wheel;
}

void timer_wheel_destroy(timer_wheel_t *wheel)
{
    if (wheel == NULL) {
        return;
    }
    for (size_t i = 0; i < TIMER_FD_BUCKET_COUNT; ++i) {
        timer_node_t *node = wheel->fd_buckets[i];

        while (node != NULL) {
            timer_node_t *next = node->hash_next;
            free(node);
            node = next;
        }
    }
    pthread_mutex_destroy(&wheel->lock);
    free(wheel->fd_buckets);
    free(wheel->slots);
    free(wheel);
}

static int track_locked(timer_wheel_t *wheel, int fd,
                        const struct timespec *now)
{
    timer_node_t *node;
    size_t bucket;

    if (find_node(wheel, fd) != NULL) {
        return -1;
    }
    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return -1;
    }
    node->fd = fd;
    node->deadline = *now;
    node->deadline.tv_sec += wheel->timeout_seconds;
    bucket = hash_fd(fd);
    node->hash_next = wheel->fd_buckets[bucket];
    wheel->fd_buckets[bucket] = node;
    link_slot(wheel, node);
    wheel->size++;
    return 0;
}

int timer_wheel_track_at(timer_wheel_t *wheel, int fd,
                         const struct timespec *now)
{
    int result;

    if (wheel == NULL || fd < 0 || now == NULL || now->tv_nsec < 0 ||
        now->tv_nsec >= 1000000000L) {
        return -1;
    }
    pthread_mutex_lock(&wheel->lock);
    result = track_locked(wheel, fd, now);
    pthread_mutex_unlock(&wheel->lock);
    return result;
}

int timer_wheel_track(timer_wheel_t *wheel, int fd)
{
    struct timespec now;
    int result;

    if (wheel == NULL || fd < 0) {
        return -1;
    }
    pthread_mutex_lock(&wheel->lock);
    result = current_time(&now) == 0 ? track_locked(wheel, fd, &now) : -1;
    pthread_mutex_unlock(&wheel->lock);
    return result;
}

int timer_wheel_track_and_publish(timer_wheel_t *wheel, int fd,
                                  timer_wheel_publish_fn publish,
                                  void *context)
{
    struct timespec now;
    int result;

    if (wheel == NULL || fd < 0 || publish == NULL) {
        return -1;
    }
    pthread_mutex_lock(&wheel->lock);
    result = current_time(&now) == 0 ? track_locked(wheel, fd, &now) : -1;
    if (result == 0 && publish(fd, context) != 0) {
        timer_node_t *node = find_node(wheel, fd);

        unlink_slot(wheel, node);
        unlink_hash(wheel, node);
        free(node);
        wheel->size--;
        result = -1;
    }
    pthread_mutex_unlock(&wheel->lock);
    return result;
}

int timer_wheel_touch_at(timer_wheel_t *wheel, int fd,
                         const struct timespec *now)
{
    timer_node_t *node;

    if (wheel == NULL || now == NULL || now->tv_nsec < 0 ||
        now->tv_nsec >= 1000000000L) {
        return -1;
    }
    pthread_mutex_lock(&wheel->lock);
    node = find_node(wheel, fd);
    if (node == NULL) {
        pthread_mutex_unlock(&wheel->lock);
        return -1;
    }
    unlink_slot(wheel, node);
    node->deadline = *now;
    node->deadline.tv_sec += wheel->timeout_seconds;
    link_slot(wheel, node);
    pthread_mutex_unlock(&wheel->lock);
    return 0;
}

int timer_wheel_touch(timer_wheel_t *wheel, int fd)
{
    struct timespec now;

    return current_time(&now) == 0
        ? timer_wheel_touch_at(wheel, fd, &now) : -1;
}

void timer_wheel_remove(timer_wheel_t *wheel, int fd)
{
    timer_node_t *node;

    if (wheel == NULL || fd < 0) {
        return;
    }
    pthread_mutex_lock(&wheel->lock);
    node = find_node(wheel, fd);
    if (node == NULL) {
        pthread_mutex_unlock(&wheel->lock);
        return;
    }
    unlink_slot(wheel, node);
    unlink_hash(wheel, node);
    free(node);
    wheel->size--;
    pthread_mutex_unlock(&wheel->lock);
}

int timer_wheel_advance_at(timer_wheel_t *wheel,
                           const struct timespec *now,
                           timer_wheel_expire_fn expire,
                           void *context)
{
    if (wheel == NULL || now == NULL || expire == NULL)
        return -1;
    pthread_mutex_lock(&wheel->lock);

    while (wheel->current_second < now->tv_sec) {
        size_t slot;
        timer_node_t *node;

        wheel->current_second++;
        slot = (size_t)((uint64_t)wheel->current_second % wheel->slot_count);
        node = wheel->slots[slot];
        while (node != NULL) {
            timer_node_t *next = node->slot_next;

            if (timespec_compare(&node->deadline, now) <= 0) {
                int fd = node->fd;

                unlink_slot(wheel, node);
                unlink_hash(wheel, node);
                free(node);
                wheel->size--;
                expire(fd, context);
            } else {
                unlink_slot(wheel, node);
                link_slot(wheel, node);
            }
            node = next;
        }
    }
    pthread_mutex_unlock(&wheel->lock);
    return 0;
}

int timer_wheel_advance(timer_wheel_t *wheel,
                        timer_wheel_expire_fn expire,
                        void *context)
{
    struct timespec now;

    return current_time(&now) == 0
        ? timer_wheel_advance_at(wheel, &now, expire, context) : -1;
}

size_t timer_wheel_size(timer_wheel_t *wheel)
{
    size_t size;

    if (wheel == NULL) {
        return 0U;
    }
    pthread_mutex_lock(&wheel->lock);
    size = wheel->size;
    pthread_mutex_unlock(&wheel->lock);
    return size;
}
