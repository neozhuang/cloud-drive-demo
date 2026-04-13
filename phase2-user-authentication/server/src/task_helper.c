#include "../include/task_helper.h"
#include "../include/do_task.h"
#include "../include/transmit_data.h"

int send_text_response(int netfd, cmd_type_t type, int status, const char *text)
{
    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = type;
    packet.status = status;
    if(text != NULL)
    {
        packet.length = strlen(text);
        if(packet.length > CONTENT_MAX) packet.length = CONTENT_MAX;
        memcpy(packet.content, text, packet.length);
    }
    return send_packet(netfd, &packet);
}

int push_path_stack(linked_stack_t * p_stack, char * p) {
    if(strcmp(p, "..") == 0) {
        char data[STACK_DATA_MAX] = {0};
        int ret = stack_pop(p_stack, data);
        if(ret == -1) {
            return -1;
        }
    }
    else if(strcmp(p, ".") == 0) {
        return 1;
    }
    else
        stack_push(p_stack, p);
    return 1;
}

void pop_path_stack(linked_stack_t * p_stack, char * dest_dir) {
    char dest_dir_temp[PATH_MAX] = {0};

    while(!stack_is_empty(p_stack)) {
        char data[PATH_MAX] = {0};
        stack_pop(p_stack, data);

        char temp[PATH_MAX] = {0};
        strcpy(temp, data);
        if(dest_dir_temp[0] != 0) {
            strcat(temp, "/");
            strcat(temp, dest_dir_temp);
        }
        strcpy(dest_dir_temp, temp);
    }
    strcpy(dest_dir, dest_dir_temp);
}

int get_short_path(char * short_path, const char * cd_dir, const char * session_path) {
    int ret;
    memset(short_path, 0, PATH_MAX);
    char stack_path[PATH_MAX] = {0};
    strcpy(stack_path, session_path);
    strcat(stack_path, "/");
    strcat(stack_path, cd_dir);

    linked_stack_t * p_stack = stack_create();
    char *p = strtok(stack_path, "/");
    do {
        ret = push_path_stack(p_stack, p);
        if(ret == -1) {
            // push_path_stack(p_stack, username); // 这里栈已经空了，可以选择将家目录路径压回去，或者直接返回错误让命令失败，这里选择后者
            break;
        }
        p = strtok(NULL, "/");
    } while(p != NULL);

    pop_path_stack(p_stack, short_path);
    stack_destroy(p_stack);
    return 0;
}

void format_mode_string(mode_t mode, char *mode_str)
{
    mode_str[0] = S_ISDIR(mode) ? 'd' : '-';
    mode_str[1] = (mode & S_IRUSR) ? 'r' : '-';
    mode_str[2] = (mode & S_IWUSR) ? 'w' : '-';
    mode_str[3] = (mode & S_IXUSR) ? 'x' : '-';
    mode_str[4] = (mode & S_IRGRP) ? 'r' : '-';
    mode_str[5] = (mode & S_IWGRP) ? 'w' : '-';
    mode_str[6] = (mode & S_IXGRP) ? 'x' : '-';
    mode_str[7] = (mode & S_IROTH) ? 'r' : '-';
    mode_str[8] = (mode & S_IWOTH) ? 'w' : '-';
    mode_str[9] = (mode & S_IXOTH) ? 'x' : '-';
    mode_str[10] = '\0';
}

int append_ll_entry(char *buffer, size_t buffer_size, const char *file_path, const char *display_name)
{
    struct stat st;
    char mode_str[11] = {0};
    char time_str[32] = {0};
    char line[256] = {0};
    struct passwd *pw = NULL;
    struct group *gr = NULL;
    struct tm *tm_info = NULL;

    if(stat(file_path, &st) == -1)
        return -1;

    format_mode_string(st.st_mode, mode_str);
    pw = getpwuid(st.st_uid);
    gr = getgrgid(st.st_gid);
    tm_info = localtime(&st.st_mtime);
    if(tm_info != NULL)
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
    else
        snprintf(time_str, sizeof(time_str), "unknown-time");

    snprintf(line,
             sizeof(line),
             "%s %2lu %-8s %-8s %8ld %s %s%s\n",
             mode_str,
             (unsigned long)st.st_nlink,
             pw != NULL ? pw->pw_name : "unknown",
             gr != NULL ? gr->gr_name : "unknown",
             (long)st.st_size,
             time_str,
             display_name,
             S_ISDIR(st.st_mode) ? "/" : "");

    if(strlen(buffer) + strlen(line) >= buffer_size)
        return 1;

    strcat(buffer, line);
    return 0;
}

static int send_stream_packet(int netfd, cmd_type_t type, const char *data, int len)
{
    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = type;
    packet.status = 0;
    packet.length = len;
    if(len > 0)
        memcpy(packet.content, data, len);
    return send_packet(netfd, &packet);
}

static int flush_tree_buffer(int netfd, char *buffer)
{
    int len = strlen(buffer);
    if(len == 0)
        return 0;
    if(send_stream_packet(netfd, TREE, buffer, len) == -1)
        return -1;
    buffer[0] = '\0';
    return 0;
}

static int append_tree_line(char *buffer, size_t buffer_size, int netfd, const char *line)
{
    size_t line_len = strlen(line);
    if(line_len >= buffer_size)
        return -1;

    if(strlen(buffer) + line_len >= buffer_size)
    {
        if(flush_tree_buffer(netfd, buffer) == -1)
            return -1;
    }

    strcat(buffer, line);
    return 0;
}

int send_tree_entries(int netfd, const char *base_path, int depth)
{
    DIR *dir = opendir(base_path);
    if(dir == NULL)
        return -1;

    struct dirent *entry = NULL;
    char buffer[CONTENT_MAX] = {0};
    while((entry = readdir(dir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[PATH_MAX] = {0};
        if(snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name) >= (int)sizeof(full_path))
        {
            closedir(dir);
            return -1;
        }

        struct stat st;
        memset(&st, 0, sizeof(st));
        if(stat(full_path, &st) == -1)
        {
            closedir(dir);
            return -1;
        }

        char line[PATH_MAX + 64] = {0};
        int offset = 0;
        for(int i = 0; i < depth; ++i)
            offset += snprintf(line + offset, sizeof(line) - offset, "  ");
        snprintf(line + offset,
                 sizeof(line) - offset,
                 "|-- %s%s\n",
                 entry->d_name,
                 S_ISDIR(st.st_mode) ? "/" : "");

        if(append_tree_line(buffer, sizeof(buffer), netfd, line) == -1)
        {
            closedir(dir);
            return -1;
        }

        if(S_ISDIR(st.st_mode))
        {
            if(flush_tree_buffer(netfd, buffer) == -1)
            {
                closedir(dir);
                return -1;
            }
            if(send_tree_entries(netfd, full_path, depth + 1) == -1)
            {
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);
    return flush_tree_buffer(netfd, buffer);
}