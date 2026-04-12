#include "../include/command_handlers.h"
#include "../include/common.h"
#include "../include/deal_command.h"
#include "../include/transmit_data.h"

#include <stdio.h>
#include <string.h>

static void print_packet_message(const packet_t *response, const char *fallback)
{
    if(response->length > 0)
    {
        printf("%.*s", response->length, response->content);
        if(response->content[response->length - 1] != '\n')
            printf("\n");
    }
    else if(fallback != NULL)
    {
        printf("%s\n", fallback);
    }
}

static int handle_pwd(const packet_t *response)
{
    if(response->status == 0)
    {
        print_packet_message(response, NULL);
    }
    else
    {
        print_packet_message(response, "pwd false!");
    }
    return 0;
}

static int handle_cd(const packet_t *response, char *prompt)
{
    if(response->status == 0)
    {
        printf("%.*s\n", response->length, response->content);
        // print last part of path as prompt
        char *last_slash = strrchr(response->content, '/');
        if(last_slash)
        {
            snprintf(prompt, PATH_MAX, "%s", last_slash + 1);
        }
        else
        {
            snprintf(prompt, PATH_MAX, "%.*s", response->length, response->content);
        }
    }
    else
    {
        print_packet_message(response, "cd false!");
    }
    return 0;
}

static int handle_ls_like(const packet_t *response, const char *name, int with_newline)
{
    if(response->status != 0)
    {
        print_packet_message(response, name);
        return 0;
    }

    if(response->length == 0) return 0;
    printf(with_newline ? "%.*s\n" : "%.*s", response->length, response->content);
    return 0;
}

static int handle_status_only_command(const packet_t *response, const char *name)
{
    if(response->status == 0)
    {
        if(response->length > 0) print_packet_message(response, NULL);
    }
    else
    {
        print_packet_message(response, name);
    }
    return 0;
}

int handle_response(int sockfd, const packet_t *response, char *prompt, const char *parameter)
{
    switch(response->type)
    {
    case PWD:
        return handle_pwd(response);
    case CD:
        return handle_cd(response, prompt);
    case LS:
        return handle_ls_like(response, "ls false!", 1);
    case LL:
        return handle_ls_like(response, "ll false!", 0);
    case MKDIR:
        return handle_status_only_command(response, "mkdir false!");
    case RMDIR:
        return handle_status_only_command(response, "rmdir false!");
    case RM:
        return handle_status_only_command(response, "rm false!");
    case PUTS:
        return handle_status_only_command(response, "puts false!");
    case GETS:
        return gets_file_client(sockfd, response, parameter, DOWNLOAD_PATH);
    case TREE:
        if(response->status != 0)
        {
            print_packet_message(response, "tree false!");
            return 0;
        }
        print_packet_message(response, NULL);
        return recv_stream_content(sockfd);
    case CAT:
        return recv_cat_stream(sockfd, response);
    case NOTCMD:
        print_packet_message(response, "Invalid command!");
        return 0;
    default:
        return 0;
    }
}