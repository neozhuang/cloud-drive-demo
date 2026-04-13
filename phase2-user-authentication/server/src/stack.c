#include "../include/stack.h"
#include <stdlib.h>
#include <string.h>

// 创建一个空的链式栈
linked_stack_t * stack_create() {
    return (linked_stack_t *)calloc(1, sizeof(linked_stack_t));
}

// 销毁一个链式栈
void stack_destroy(linked_stack_t * p_stack) {
    if(p_stack == NULL) {
        return;
    }
    stack_node_t * p_node = p_stack->top;
    while(p_node != NULL) {
        stack_node_t * tmp = p_node->next;
        free(p_node);
        p_node = tmp;
    }
    free(p_stack);
}

// 判断栈是否为空
bool stack_is_empty(const linked_stack_t * p_stack) {
    return p_stack->top == NULL;
}

// 入栈操作
void stack_push(linked_stack_t * p_stack, const char * data) {
    stack_node_t * new_node = (stack_node_t *)calloc(1, sizeof(stack_node_t));
    if(new_node == NULL) {
        return;
    }
    strcpy(new_node->data, data);
    new_node->next = p_stack->top;
    p_stack->top = new_node;
}

// 出栈操作
int stack_pop(linked_stack_t * p_stack, char * data) {
    if(stack_is_empty(p_stack)) {
        return -1;
    }
    stack_node_t * tmp = p_stack->top;
    strcpy(data, tmp->data);
    p_stack->top = tmp->next;
    free(tmp);
    return 0;
}

// 访问栈顶元素
char * stack_peek(const linked_stack_t * p_stack) {
    if(stack_is_empty(p_stack)) {
        return NULL;
    }
    return p_stack->top->data;
}