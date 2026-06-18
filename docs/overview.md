# Cloud Drive Demo

## Phase-01

### Feature Requirements

#### Basic Framework and Commands

Client:

```plaintext
build connection with the server:
client reads standard input:
// read command line input
// parse the content readed, get the command type and command args
// identify them whether correct, filter invalid content 
// send command type and args to the server

client can input the available commands as follows:
// pwd
// cd 
// ls
// mkdir
// rmdir
// rm
// puts 
// gets 
// ...
```

Server:

```plaintext
launch the server, accept the client's connection request:
// accept the client fd, deliver it to the subthread
subthreads wait client requests:
// client send commands, server receive commands
// response by according commands
```

#### Server Logging

```plaintext
// client connection time
// client requests information
// client operatoration records(e.x. rm file, upload file, when, ...)
```

#### Config Files

There must be some configuration files to configure the project's basic information. This configuration file records some configuration information (we can choose not to commit: i.e., Git ignores this configuration file), and then allows our code to dynamically load information such as IP address, port, database password, and log level into the code for easy use.

#### Configurable Log

We know that logs have multiple levels. In general, during daily code development, to better track code execution and facilitate code writing and bug debugging, we set the log level to a relatively low level (e.g., INFO, DEBUG) to print as much information as possible for tracing code execution.

However, printing so much log information is impractical in a production environment. Firstly, some INFO or DEBUG messages are unnecessary in a production environment (the user-facing environment) (this information is used for detailed code execution tracking). Secondly, lower log levels result in more printed information, causing our program to spend a lot of time printing logs, leading to wasted server resources.

Therefore, ideally, we want the same code to dynamically select the log level based on different situations (e.g., development, testing, production).

Combining the configuration options in the configuration file above, when the program runs, we first read the currently set log level from the configuration file (for example, developers might set the LOG option to LOG=INFO in the configuration file, while production environments for users would set LOG to LOG=ERROR).

We can let the macro function selectively print logs based on the log level read from the configuration file (i.e., if the log level passed to the log macro function is lower than the log level read from the configuration file, the macro function returns directly without printing).

This also requires that when calling the macro function in the code, we should pass different log levels to the macro function for different printing situations. 

