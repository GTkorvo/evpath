#define RS_PORT 5357

/* define the message type */
#define REGISTER 'a'
#define REQUEST 'b'
#define NO_NEED_REGISTER 'c'
#define REDIRECTION_IMPOSSIBLE 'd'
#define REDIRECTION_REQUESTED 'e'

typedef struct _redirect_msg {
    char type;
    char *content;
} redirect_msg, *redirect_msg_ptr;

