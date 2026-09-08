#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(8888);
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        perror("connect failed");
        close(sock);
        return 1;
    }

    char username[32];
    printf("Enter username: ");
    scanf("%31s", username);
    getchar(); // 移除多出的換行
    send(sock, username, sizeof(username), 0);

    char buffer[BUF_SIZE];
    int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        printf("Server closed connection.\n");
        close(sock);
        return 1;
    }
    buffer[n] = '\0';
    printf("%s\n\n", buffer);

    while(1) {
        printf("> ");
        fflush(stdout);

        char cmd[BUF_SIZE];
        if(!fgets(cmd, BUF_SIZE, stdin))
            break;
        cmd[strcspn(cmd,"\n")] = 0; // 去掉換行

        if(strncmp(cmd,"exit",4)==0)
            break;

        char op[32] = {0}, filename[64] = {0}, arg[64] = {0};
        sscanf(cmd,"%31s %63s %63s", op, filename, arg);

        // 送出整個命令字串（包含結尾 '\0'）
        send(sock, cmd, strlen(cmd)+1, 0);

        // -----------------------------
        // 處理 read 指令
        // -----------------------------
        if(strcmp(op,"read")==0) {
            // 先偷看前 3 個位元組，判斷是不是 "OK\0"
            n = recv(sock, buffer, 3, MSG_PEEK);
            if (n <= 0) {
                printf("Connection closed by server.\n");
                break;
            }

            if (n == 3 && buffer[0]=='O' && buffer[1]=='K' && buffer[2]=='\0') {
                // 確定是 OK，真正把 "OK\0" 吃掉
                n = recv(sock, buffer, 3, 0);
                (void)n; // 避免未使用警告

                int filesize = 0;
                // 再收檔案大小（int）
                n = recv(sock, &filesize, sizeof(int), 0);
                if (n <= 0) {
                    printf("Failed to receive filesize.\n");
                    continue;
                }

                if(filesize == 0){
                    printf("(empty file)\n");
                    continue;
                }

                char *buf = (char*)malloc(filesize);
                if (!buf) {
                    printf("malloc failed\n");
                    // 把後面的檔案內容吃掉丟棄，以維持通訊一致
                    int recvd = 0;
                    while (recvd < filesize) {
                        int chunk = (filesize - recvd > BUF_SIZE) ? BUF_SIZE : (filesize - recvd);
                        n = recv(sock, buffer, chunk, 0);
                        if (n <= 0) break;
                        recvd += n;
                    }
                    continue;
                }

                int recvd = 0;
                while (recvd < filesize) {
                    int chunk = (filesize - recvd > BUF_SIZE) ? BUF_SIZE : (filesize - recvd);
                    n = recv(sock, buf + recvd, chunk, 0);
                    if (n <= 0) break;
                    recvd += n;
                }

                // 直接依照 bytes 數輸出（避免被 '\0' 截斷）
                write(STDOUT_FILENO, buf, recvd);
                printf("\n");
                free(buf);
            } else {
                // 不是 "OK\0"，代表是錯誤訊息，直接讀完整訊息
                n = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (n > 0) {
                    buffer[n] = '\0';
                    printf("%s\n", buffer);
                } else {
                    printf("Failed to receive error message.\n");
                }
            }
        }

        // -----------------------------
        // 處理 write 指令
        // -----------------------------
        else if(strcmp(op,"write")==0) {
            // 收確認訊息（這裡 server 只會先送文字，不會立即跟 binary）
            n = recv(sock, buffer, sizeof(buffer)-1, 0);
            if (n <= 0) {
                printf("Server closed connection.\n");
                break;
            }
            buffer[n] = '\0';
            if(strcmp(buffer,"OK")!=0){
                printf("%s\n", buffer);
                continue;
            }

            printf("Enter content (end with Ctrl+D):\n");

            char *input = NULL;
            size_t len = 0;
            ssize_t readlen;
            char *total = NULL;
            int totalsize = 0;

            // 讀使用者輸入直到 Ctrl+D
            while((readlen = getline(&input,&len,stdin)) != -1){
                char *tmp = realloc(total, totalsize + (int)readlen);
                if (!tmp) {
                    free(total);
                    total = NULL;
                    totalsize = 0;
                    break;
                }
                total = tmp;
                memcpy(total + totalsize, input, readlen);
                totalsize += (int)readlen;
            }
            free(input);

            // 把長度與內容送給 server
            send(sock, &totalsize, sizeof(int), 0);
            if(totalsize > 0 && total != NULL)
                send(sock, total, totalsize, 0);
            free(total);

            // 再收一次寫入結果
            n = recv(sock, buffer, sizeof(buffer)-1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                printf("%s\n", buffer);
            } else {
                printf("Server closed connection.\n");
                break;
            }
        }

        // -----------------------------
        // 其他指令 (new, change 等)
        // -----------------------------
        else {
            n = recv(sock, buffer, sizeof(buffer)-1, 0);
            if (n <= 0) {
                printf("Server closed connection.\n");
                break;
            }
            buffer[n] = '\0';
            printf("%s\n", buffer);
        }
    }

    close(sock);
    return 0;
}
