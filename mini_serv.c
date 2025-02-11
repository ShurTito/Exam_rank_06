#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <sys/select.h> // Includes
#include <stdio.h>
#include <stdlib.h>

int id_client = 0;  // Variable que lleva la cuenta de los clientes conectados
int fd_max = 0;  // Variable para mantener el valor del descriptor de archivo más grande (usado en select)
int cl_fd[4096] = {-1};  // Arreglo que guarda los descriptores de archivos de los clientes
int cl_id_fd[4096] = {-1};  // Arreglo que mapea cada descriptor de archivo a un ID único
char *extract_id[4096] = {0};  // Arreglo para almacenar los mensajes que aún no han sido procesados

void erro_msg(char *m) {
    write(2, m, strlen(m));  // Muestra un mensaje de error
    exit(1);
}

void max_fd() {
    // Recorre los descriptores de clientes y obtiene el más grande para usarlo con select()
    for (int i = 0; i < id_client; i++) {
        if (cl_fd[i] > fd_max)
            fd_max = cl_fd[i];
    }
}

void send_all(char *msg, int fd) {
    // Envia un mensaje a todos los clientes, excepto al cliente cuyo descriptor es 'fd'
    for (int i = 0; i < id_client; i++) {
        if (cl_fd[i] != fd && cl_fd[i] != -1)
            send(cl_fd[i], msg, strlen(msg), 0);
    }
}

int extract_message(char **buf, char **msg) {
    char *newbuf;
    int i;

    *msg = 0;
    if (*buf == 0)
        return (0);
    i = 0;
    while ((*buf)[i]) {
        if ((*buf)[i] == '\n') {  // Detecta el fin de un mensaje (salto de línea)
            newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
            if (newbuf == 0)
                return (-1);  // Error en la asignación de memoria
            strcpy(newbuf, *buf + i + 1);
            *msg = *buf;
            (*msg)[i + 1] = 0;  // Finaliza el mensaje con el salto de línea
            *buf = newbuf;
            return (1);  // Mensaje completo
        }
        i++;
    }
    return (0);  // No se encontró un mensaje completo
}

char *str_join(char *buf, char *add) {
    char *newbuf;
    int len;

    if (buf == 0)
        len = 0;
    else
        len = strlen(buf);
    newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
    if (newbuf == 0)
        return (0);  // Error en la asignación de memoria
    newbuf[0] = 0;
    if (buf != 0)
        strcat(newbuf, buf);
    free(buf);
    strcat(newbuf, add);
    return (newbuf);
}

int main(int argc, char **argv) {
    // Verifica que el número de argumentos sea correcto
    if (argc != 2)
        erro_msg("Wrong number of arguments\n");

    int sockfd, connfd;
    socklen_t len;
    struct sockaddr_in servaddr, cli;

    // Creación del socket y verificación de error
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        erro_msg("Fatal error\n");

    bzero(&servaddr, sizeof(servaddr));

    // Inicialización de los sets de 'select'
    fd_set readfds, writefds, cpyfds;

    FD_ZERO(&cpyfds);  // Inicializa el conjunto de descriptores de archivo
    FD_SET(sockfd, &cpyfds);  // Agrega el socket principal al conjunto

    fd_max = sockfd;  // Establece fd_max al valor del socket

    // Asignación de IP y puerto
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(2130706433);  // 127.0.0.1
    servaddr.sin_port = htons(atoi(argv[1]));  // Puerto tomado como argumento

    // Enlace del socket a la IP y puerto
    if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        erro_msg("Fatal error\n");
    }
    if (listen(sockfd, 10) != 0) {
        erro_msg("Fatal error\n");
    }

    while (1) {
        readfds = writefds = cpyfds;
        // Llama a select para monitorear los descriptores de archivo
        if (select(fd_max + 1, &readfds, &writefds, 0, 0) >= 0) {
            for (int fd = sockfd; fd <= fd_max; fd++) {
                if (FD_ISSET(fd, &readfds)) {  // Si hay datos para leer
                    if (fd == sockfd) {
                        // Acepta nuevas conexiones de clientes
                        len = sizeof(cli);
                        connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
                        if (connfd >= 0) {
                            int id = id_client++;  // Asigna un ID único para cada cliente
                            cl_id_fd[connfd] = id;  // Mapea el descriptor al ID
                            cl_fd[id] = connfd;  // Asocia el ID al cliente
                            char msg[50] = {'\0'};
                            sprintf(msg, "server: client %d just arrived\n", id);
                            send_all(msg, connfd);  // Informa a todos los clientes de la llegada
                            FD_SET(connfd, &cpyfds);  // Agrega el descriptor al conjunto de select
                            max_fd();  // Actualiza fd_max
                            break;
                        }
                    } else {
                        // Maneja la recepción de mensajes de clientes
                        int id = cl_id_fd[fd];
                        char buffer[4096] = {'\0'};
                        int readed = recv(fd, &buffer, 4096, MSG_DONTWAIT);
                        buffer[readed] = '\0';
                        if (readed <= 0) {  // Si no se reciben datos o la conexión se cierra
                            char msg[50] = {'\0'};
                            sprintf(msg, "server: client %d just left\n", id);
                            send_all(msg, fd);  // Informa a todos de la desconexión
                            FD_CLR(fd, &cpyfds);  // Elimina el descriptor de select
                            close(fd);  // Cierra el descriptor de archivo
                            cl_fd[id] = -1;  // Marca al cliente como desconectado
                            free(extract_id[id]);  // Libera el mensaje no procesado
                        } else {
                            char *msg_to_send = 0;
                            extract_id[id] = str_join(extract_id[id], buffer);  // Acumula los mensajes recibidos
                            while (extract_message(&extract_id[id], &msg_to_send)) {
                                char msg[50] = {'\0'};
                                sprintf(msg, "client %d: ", id);
                                send_all(msg, fd);  // Envía el mensaje a todos los clientes
                                send_all(msg_to_send, fd);
                                free(msg_to_send);  // Libera el mensaje después de enviarlo
                            }
                        }
                    }
                }
            }
        }
    }
}

