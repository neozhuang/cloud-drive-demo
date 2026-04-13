#ifndef __DO_TASK_H__
#define __DO_TASK_H__

#include "block_queue.h"

int do_task(node_t * p_node); 

int pwd_dir(int netfd, const char * session_path);
int cd_dir(int netfd, const char * cd_dir, const char * session_path);

int ls_dir(int netfd, const char * ls_dir, const char * session_path);
int ll_dir(int netfd, const char * ll_dir, const char * session_path);

int mk_dir(int netfd, const char * mk_dir, const char * session_path);
int rm_dir(int netfd, const char * rm_dir, const char * session_path);
int rm_file(int netfd, const char * rm_file, const char * session_path);

int tree_dir(int netfd, const char * tree_dir, const char * session_path);
int cat_file(int netfd, const char * cat_file, const char * session_path);

int notcmd(int netfd);

#endif /* __DO_TASK_H__ */
