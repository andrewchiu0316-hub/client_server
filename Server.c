#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 20
#define MAX_FILES 100
#define BUF_SIZE 1024

// ---------------------
//   User Table
// ---------------------
typedef struct {
    char name[32];
    char group[32];
} User;

User users[] = {
    {"Ken", "AOS"},
    {"Barbie", "AOS"},
    {"Alex", "AOS"},
    {"John", "CSE"},
    {"Mary", "CSE"},
    {"Tom", "CSE"}
};
int USER_COUNT = 6;

char* find_group(char *username) {
    for (int i = 0; i < USER_COUNT; i++)
        if (strcmp(users[i].name, username) == 0)
            return users[i].group;
    return NULL;
}

// ---------------------
//   File Entry
// ---------------------
typedef struct {
    char owner[32];
    char group[32];
    char filename[64];
    int perm[6];    // owner_r, owner_w, group_r, group_w, other_r, other_w
    char *content;
    int size;
    pthread_rwlock_t rwlock;
} FileEntry;

FileEntry files[MAX_FILES];
int file_count = 0;
pthread_mutex_t file_table_lock = PTHREAD_MUTEX_INITIALIZER;

// ---------------------
//   Helper Functions
// ---------------------
void show_capability() {
    printf("\n===== Capability List =====\n");
    for (int i = 0; i < file_count; i++) {
        printf("%d) %s owner:%s group:%s perm:%d%d%d%d%d%d size:%d\n",
            i,
            files[i].filename,
            files[i].owner,
            files[i].group,
            files[i].perm[0], files[i].perm[1],
            files[i].perm[2], files[i].perm[3],
            files[i].perm[4], files[i].perm[5],
            files[i].size
        );
    }
    printf("===========================\n");
}

int find_file(char *name) {
    for (int i = 0; i < file_count; i++)
        if (strcmp(files[i].filename, name) == 0)
            return i;
    return -1;
}

int has_read_perm(FileEntry *f, char *user, char *group) {
    if (strcmp(f->owner, user) == 0) return f->perm[0];
    if (strcmp(f->group, group) == 0) return f->perm[2];
    return f->perm[4];
}

int has_write_perm(FileEntry *f, char *user, char *group) {
    if (strcmp(f->owner, user) == 0) return f->perm[1];
    if (strcmp(f->group, group) == 0) return f->perm[3];
    return f->perm[5];
}

// ---------------------
// Save / Load file
// ---------------------
void save_to_disk(FileEntry *f) {
    mkdir("data", 0777);

    char path[128], meta[128];
    snprintf(path, sizeof(path), "data/%s", f->filename);
    snprintf(meta, sizeof(meta), "data/%s.meta", f->filename);

    FILE *fp = fopen(path, "wb");
    if(fp) {
        fwrite(f->content, 1, f->size, fp);
        fclose(fp);
    }

    FILE *mp = fopen(meta, "w");
    if(mp) {
        fprintf(mp, "owner:%s\n", f->owner);
        fprintf(mp, "group:%s\n", f->group);
        fprintf(mp, "perm:%d%d%d%d%d%d\n",
                f->perm[0],f->perm[1],f->perm[2],
                f->perm[3],f->perm[4],f->perm[5]);
        fclose(mp);
    }
}

void load_from_disk() {
    system("mkdir -p data");  // 確保資料夾存在

    DIR *d = opendir("data");
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_type != DT_REG) continue; // 只讀檔案
        if (file_count >= MAX_FILES) break;
        if (strstr(entry->d_name,".meta")) continue; // 跳過 meta 檔

        char path[256];
        snprintf(path, sizeof(path), "data/%s", entry->d_name);

        FILE *fp = fopen(path, "rb");
        if (!fp) continue;

        fseek(fp,0,SEEK_END);
        int size = ftell(fp);
        fseek(fp,0,SEEK_SET);

        FileEntry *f = &files[file_count];
        strcpy(f->filename, entry->d_name);

        f->content = malloc(size);
        fread(f->content, 1, size, fp);
        fclose(fp);
        f->size = size;

        // 預設 owner/group/perm
        strcpy(f->owner,"Ken");
        strcpy(f->group,"AOS");
        for(int i=0;i<6;i++) f->perm[i]=1;

        pthread_rwlock_init(&f->rwlock,NULL);
        file_count++;
    }
    closedir(d);
    show_capability();
}

// ---------------------
//   Client Thread
// ---------------------
void *client_handler(void *sockptr) {
    int sock = *(int*)sockptr;
    free(sockptr);

    char user[32];
    recv(sock, user, sizeof(user), 0);

    char *group = find_group(user);
    if (!group) {
        char *msg = "Invalid user. Connection closed.";
        send(sock, msg, strlen(msg)+1, 0);
        close(sock);
        return NULL;
    }

    printf("[+] Client logged in: %s (%s)\n", user, group);
    send(sock, "OK", 3, 0);

    char cmd[BUF_SIZE];

    while(1) {
        memset(cmd, 0, sizeof(cmd));
        if(recv(sock, cmd, sizeof(cmd), 0)<=0) break;

        char op[32], filename[64], arg[64];
        sscanf(cmd,"%s %s %s",op,filename,arg);

        // ------------------------------
        //   NEW
        // ------------------------------
        if(strcmp(op,"new")==0){
            pthread_mutex_lock(&file_table_lock);
            if(find_file(filename)!=-1){
                send(sock,"File exists.",20,0);
                pthread_mutex_unlock(&file_table_lock);
                continue;
            }

            FileEntry *f = &files[file_count];
            strcpy(f->filename, filename);
            strcpy(f->owner, user);
            strcpy(f->group, group);
            for(int i=0;i<6;i++) f->perm[i]=(arg[i]=='r'||arg[i]=='w')?1:0;
            f->content=NULL; f->size=0;
            pthread_rwlock_init(&f->rwlock,NULL);
            file_count++;

            char path[128];
            snprintf(path,sizeof(path),"data/%s",filename);
            FILE *fp=fopen(path,"wb"); if(fp) fclose(fp);
            save_to_disk(f);

            pthread_mutex_unlock(&file_table_lock);
            show_capability();
            send(sock,"File created.",20,0);
        }

        // ------------------------------
        //   READ
        // ------------------------------
        else if(strcmp(op,"read")==0){
            pthread_mutex_lock(&file_table_lock);
            int idx = find_file(filename);
            if(idx==-1){ pthread_mutex_unlock(&file_table_lock); send(sock,"File does not exist.",30,0); continue; }
            FileEntry *f=&files[idx];
            if(!has_read_perm(f,user,group)){ pthread_mutex_unlock(&file_table_lock); send(sock,"Permission denied.",30,0); continue; }
            pthread_mutex_unlock(&file_table_lock);

            pthread_rwlock_rdlock(&f->rwlock);

            // 如果 f->content 為 NULL 從 disk 讀取
            if(!f->content){
                char path[256];
                snprintf(path,sizeof(path),"data/%s",filename);
                FILE *fp=fopen(path,"rb");
                if(fp){
                    fseek(fp,0,SEEK_END);
                    int size=ftell(fp);
                    fseek(fp,0,SEEK_SET);
                    f->content = malloc(size);
                    fread(f->content,1,size,fp);
                    fclose(fp);
                    f->size=size;
                } else {
                    pthread_rwlock_unlock(&f->rwlock);
                    send(sock,"Failed to open file.",30,0);
                    continue;
                }
            }

            send(sock,"OK",3,0);
            send(sock,&f->size,sizeof(int),0);
            int sent=0;
            while(sent<f->size){
                int chunk=(f->size-sent>BUF_SIZE)?BUF_SIZE:(f->size-sent);
                send(sock,f->content+sent,chunk,0);
                sent+=chunk;
            }
            pthread_rwlock_unlock(&f->rwlock);
        }

        // ------------------------------
        //   WRITE / APPEND
        // ------------------------------
        else if(strcmp(op,"write")==0){
            pthread_mutex_lock(&file_table_lock);
            int idx = find_file(filename);
            if(idx==-1){ pthread_mutex_unlock(&file_table_lock); send(sock,"File does not exist.",30,0); continue; }
            FileEntry *f=&files[idx];
            if(!has_write_perm(f,user,group)){ pthread_mutex_unlock(&file_table_lock); send(sock,"Permission denied.",30,0); continue; }
            pthread_mutex_unlock(&file_table_lock);

            pthread_rwlock_wrlock(&f->rwlock);
            send(sock,"OK",3,0);

            int recv_size; recv(sock,&recv_size,sizeof(int),0);
            char *buf = malloc(recv_size);
            int recvd=0;
            while(recvd<recv_size){
                int n=recv(sock,buf+recvd,recv_size-recvd,0);
                if(n<=0) break;
                recvd+=n;
            }

            if(arg[0]=='o'){ free(f->content); f->content=buf; f->size=recv_size; }
            else { f->content=realloc(f->content,f->size+recv_size); memcpy(f->content+f->size,buf,recv_size); f->size+=recv_size; free(buf); }

            save_to_disk(f);
            pthread_rwlock_unlock(&f->rwlock);
            show_capability();
            send(sock,"Write OK",20,0);
        }

        // ------------------------------
        //   CHANGE PERMISSION
        // ------------------------------
        else if(strcmp(op,"change")==0){
            pthread_mutex_lock(&file_table_lock);
            int idx=find_file(filename);
            if(idx==-1){ pthread_mutex_unlock(&file_table_lock); send(sock,"File does not exist.",30,0); continue; }
            FileEntry *f=&files[idx];
            if(strcmp(f->owner,user)!=0){ pthread_mutex_unlock(&file_table_lock); send(sock,"Only owner can change permission.",40,0); continue; }
            for(int i=0;i<6;i++) f->perm[i]=(arg[i]=='r'||arg[i]=='w')?1:0;
            save_to_disk(f);
            pthread_mutex_unlock(&file_table_lock);
            show_capability();
            send(sock,"Permission updated.",40,0);
        }

        else send(sock,"Unknown command.",30,0);
    }

    printf("[-] Client disconnected: %s\n", user);
    close(sock);
    return NULL;
}

// ---------------------
//   Main
// ---------------------
int main() {
    load_from_disk();

    int server_fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(8888);
    addr.sin_addr.s_addr=INADDR_ANY;

    bind(server_fd,(struct sockaddr*)&addr,sizeof(addr));
    listen(server_fd,20);

    printf("Server running on port 8888...\n");

    while(1){
        int *client_sock=malloc(sizeof(int));
        *client_sock=accept(server_fd,NULL,NULL);
        pthread_t t; pthread_create(&t,NULL,client_handler,client_sock);
        pthread_detach(t);
    }

    return 0;
}
