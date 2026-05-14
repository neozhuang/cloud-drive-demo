#include <stdio.h>

#include "domain/session.h"
#include "config/config.h"
#include "transport/connection.h"
#include "ui/menu.h"
#include "ui/term_ui.h"

int main(int argc, char *argv[])
{
    const char *config_path = argc > 1 ? argv[1] : NULL;
    ClientConfig config;
    int sockfd;
    ClientContext context;

    client_term_ui_print_banner();
    client_term_ui_print_start_time();

    if (client_config_load(&config, config_path) != 0) {
        fprintf(stderr, "Failed to load client config: %s\n",
                config_path == NULL ? CLIENT_CONFIG_DEFAULT_PATH : config_path);
        return 1;
    }
    // client_config_print(&config);

    client_context_init(&context);

    sockfd = client_net_connect(&config.server, &context);
    if (sockfd < 0) {
        perror("Failed to connect server");
        return 1;
    }

    printf("Connected to %s:%d\n", config.server.host, config.server.port);

    int ret = client_term_ui_run_main_menu(&context);
    if(ret == 0){
        // login succeed
        // start event loop
        context.state = CLIENT_STATE_IDLE; 
        
        getchar();
        // event loop
    }

    client_net_close(sockfd);
    return 0;
}
