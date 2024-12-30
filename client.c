#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/sha.h>

#define PORT 3000
#define BUFFER_SIZE 1024

char username[50], password[50];
char buffer[BUFFER_SIZE];
int choice;
char current_token[SHA256_DIGEST_LENGTH * 2 + 1] = {0};
int in_game = 0;

void show_menu() {
    printf("\n-----Menu-----\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Join Game\n");
    printf("4. Exit\n");
    printf("--------------\n");
    printf("Enter your choice: ");
}

void register_packet() {
    bzero(buffer, sizeof(buffer));
    printf("Enter username: ");
    scanf("%s", username);
    printf("Enter password: ");
    scanf("%s", password);
    snprintf(buffer, sizeof(buffer), "REGISTER %s %s", username, password);
}

void login_packet(){
    bzero(buffer, sizeof(buffer));
    printf("Enter username: ");
    scanf("%s", username);
    printf("Enter password: ");
    scanf("%s", password);
    snprintf(buffer, sizeof(buffer), "LOGIN %s %s", username, password);
}

void join_game_packet() {
    if (strlen(current_token) == 0) {
        printf("Please login first to get a token.\n");
        show_menu();
    }else{
        snprintf(buffer, sizeof(buffer), "JOINGAME %s", current_token);
    }
}

void answer_game_packet(char *questionID, int sock) {
    if (strlen(current_token) == 0) {
        printf("Please login first to get a token.\n");
        return;
    }
    char answer[3];
    char *question_id = (char *)malloc(2);
    question_id[0] = questionID[0];
    question_id[1] = '\0';

    printf("Enter your answer (1-4): ");
    scanf("%s", answer);
    bzero(buffer, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "ANSWER %s %s %s", current_token, question_id, answer);
    printf("%s\n", question_id);

//    printf("%llu\n", strlen(buffer));
    printf("Sending packet: '%s'\n", buffer);
    send(sock, buffer, strlen(buffer), 0);
    printf("Sended\n");
}

int main() {
    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    // Change to your local IP or localhost
    server_addr.sin_addr.s_addr = inet_addr("192.168.234.105");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(1);
    }

    printf("Connected to server.\n");

    while (1) {
        bzero(buffer, sizeof(buffer));
        if(in_game != 1) {
            show_menu();
            scanf("%d", &choice);
            switch (choice) {
                case 1:
                    register_packet();
                    break;
                case 2:
                    login_packet();
                    break;
                case 3:
                    join_game_packet();
                    break;
                case 4:
                    close(sock);
                    exit(0);
                default:
                    printf("Invalid choice\n");
                    continue;
            }
        }
        // Send packet
        if (strlen(buffer) > 0) {
            printf("Sending packet: '%s'\n", buffer);
            send(sock, buffer, strlen(buffer), 0);
        }

        // Clear and receive response
        bzero(buffer, sizeof(buffer));
        recv(sock, buffer, BUFFER_SIZE, 0);
        printf("Server Response: %s\n", buffer);

        char *fcode = strtok(buffer, "|");
        // Special handling for register
        if (strncmp(fcode, "REGISTER", 9) == 0)
        {
            char *message = strtok(NULL, "|");
            printf("%s\n", message);
        }
        if (strncmp(fcode, "LOGIN", 6) == 0)
        {
            char *message = strtok(NULL, "|");
            if(strncmp(message, "Login successful!", 17) == 0)
            {
                char *token = strtok(NULL, "|");
                strcpy(current_token, token);
                printf("You have successfully logged in!\n");
                printf("Your game token is: %s\n", current_token);
            } else {
                printf("%s\n", message);
            }

        }

        // Handle game join
        if (strncmp(fcode, "JOINGAME", 9) == 0) {
            char *message = strtok(NULL, "|");
            printf("%s\n", message);
            if (strncmp(message, "You have joined game successfully!", 35) == 0) {
                in_game = 1;
                printf("Game started!\n");
            }
        }
        // Handle game start and results
        if (strncmp(fcode, "STARTGAME", 10) == 0) {
            char *questionID = strtok(NULL, "|");
            char *question = strtok(NULL, "|");
            char *option1 = strtok(NULL, "|");
            char *option2 = strtok(NULL, "|");
            char *option3 = strtok(NULL, "|");
            char *option4 = strtok(NULL, "|");
            printf("This is the question to choose the main character of the game.\n");
            printf("\nQuestion ID: %s\n%s\n", questionID, question);
            printf("1. %s\n2. %s\n3. %s\n4. %s\n", option1, option2, option3, option4);
            answer_game_packet(questionID, sock);
        } 
        else if (strncmp(fcode, "QUESTION", 8) == 0) {
            char *questionID = strtok(NULL, "|");
            char *question = strtok(NULL, "|");
            char *option1 = strtok(NULL, "|");
            char *option2 = strtok(NULL, "|");
            char *option3 = strtok(NULL, "|");
            char *option4 = strtok(NULL, "|");
            printf("\nQuestion ID: %s\n%s\n", questionID, question);
            printf("1. %s\n2. %s\n3. %s\n4. %s\n", option1, option2, option3, option4);
            answer_game_packet(questionID, sock);
        }
        else if (strcmp(fcode, "GAMEOVER") == 0) {
            printf("Game Result: %s\n", buffer);
            in_game = 0;
        }

        if (strcmp(buffer, "invalid command") == 0) {
            printf("Invalid command\n");
        }

        if (strcmp(fcode, "ANSWER") == 0)
        {
            char *message = strtok(NULL, "|");
            if(strncmp(message, "Correct!", 8) == 0)
            {
                printf("You have answered correctly!\n");
            }
            else
            {
                printf("You have answered incorrectly!\n");
            }
        }
        

        if (strcmp(fcode, "MAINPLAYER") == 0)
        {
            char *token = strtok(NULL, "|");
            if(strcmp(token, current_token) == 0)
            {
                printf("You are the main player!\n");
            }
            else
            {
                printf("You are not the main player!\n");
            }
        }

        if (strcmp(fcode, "ROUNDRESULT") == 0){
            char *message = strtok(NULL, "|");
            if(strcmp(message, "MAINPLAYER_WIN") == 0){
                char *token = strtok(NULL, "|");
                if(strcmp(token, current_token) == 0){
                    printf("You win the game!\n");
                } else {
                    printf("You are defeated!\n");
                }
                in_game = 0;
            }else if(strcmp(message, "NEWMAINPLAYER") == 0){
                char *token = strtok(NULL, "|");
                if(strcmp(token, current_token) == 0){
                    printf("You are new main player!\n");
                } else {
                    printf("Main player in the game changed!\n");
                }
            }else{
                printf("No one wins the game!\n");
                in_game = 0;
            }
        }
        
    }

    close(sock);
    return 0;
}