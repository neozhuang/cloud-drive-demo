#ifndef __STACK_H__
#define __STACK_H__
#include <stdbool.h>
#define STACK_DATA_MAX 256

typedef struct stack_node_s {
    char data[STACK_DATA_MAX]; 
    struct stack_node_s * next;
} stack_node_t;

typedef struct {
    stack_node_t * top; // 指向栈顶
} linked_stack_t;

// 基本操作
// 创建一个空的链式栈
linked_stack_t * stack_create();
// 销毁一个链式栈
void stack_destroy(linked_stack_t * p_stack);
// 判断栈是否为空
bool stack_is_empty(const linked_stack_t * p_stack);
// 入栈操作
void stack_push(linked_stack_t * p_stack, const char * data);
// 出栈操作
int stack_pop(linked_stack_t * p_stack, char * data);
// 访问栈顶元素
char * stack_peek(const linked_stack_t * p_stack);

#endif /* __STACK_H__ */