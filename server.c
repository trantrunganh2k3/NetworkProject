#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <time.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <unistd.h> // For usleep
#include <sys/select.h>

#define PORT 3000
#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024
#define DATA_FILE "users.txt"
#define TOKEN_SIZE 32
#define MIN_PLAYERS 3
#define MAX_QUESTIONS 10

typedef struct {
    char token[SHA256_DIGEST_LENGTH * 2 + 1];
    int client_sock;
    int answered;
    int correct;
    time_t answer_time;
    float points;
    int eliminated;
} Player;

typedef struct {
    int socket;
} client_t;

typedef struct {
    int client_sock;
} ThreadArgs;

typedef struct {
    char username[50];
    char password[50];
} User;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
fd_set read_fds;

client_t clients[MAX_CLIENTS];
Player active_players[100];
User users[100];
int user_count = 0;
Player *main_player = NULL;
int client_count = 0;

int active_player_count = 0;
int game_in_progress = 0; // 0: Không có trò chơi, 1: Đang diễn ra
int main_player_choice = 0; // 0: Chưa chọn, 1: Chọn đúng
int number_of_questions = 0;
int eliminated_player_count = 0; // Số người chơi bị loại
int skipped_count = 0;
char* main_player_answer;

typedef struct {
    char question[256];
    char answers[4][64];
    char correct_answers[2];
} Question;

Question questions[MAX_QUESTIONS] = {
    {
        "Thủ đô của Việt Nam là gì?",
        {"Hà Nội", "Thành Phố Hồ Chí Minh", "Đà Nẵng", "Hải Phòng"},
        "1"
    },
    {
        "Tính đến năm 2024, đã có bao nhiêu khóa sinh viên ĐHBKHN?",
        {"69", "70", "68", "71"},
        "1"
    },
    {
        "Có bao nhiêu bang ở Mỹ?",
        {"49", "51", "50", "52"},
        "3"
    },
    {
        "Đại học Bách Khoa Hà Nội được thành lập vào năm nào?",
        {"1958", "1956", "1960", "1967"},
        "2"
    },
    {
        "Ai đã vẽ bức tranh Mona Lisa?",
        {"Van Gogh", "Da Vinci", "Picasso", "Rembrandt"},
        "2"
    },
    {
        "Có bao nhiêu châu lục trên thế giới?",
        {"5", "7", "6", "4"},
        "3"
    },
    {
        "Nguyên tố nào phổ biến nhất?",
        {"Helium", "Carbon", "Oxygen", "Hydrogen"},
        "4"
    },
    {
        "Căn bậc 2 của 144 là bao nhiêu?",
        {"10", "11", "12", "13"},
        "3"
    },
    {
        "Ai đã viết 'Romeo and Juliet'?",
        {"Charles Dickens", "William Shakespeare", "Jane Austen", "Mark Twain"},
        "2"
    },
    {
        "Đại học Bách Khoa Hà Nội có bao nhiêu cổng?",
        {"2", "3", "4", "5"},
        "3"
    }
};

void generate_token(char *token, const char *username, int client_sock) {
    char data[256];
    snprintf(data, sizeof(data), "%s_%d_%ld", username, client_sock, time(NULL));

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)data, strlen(data), hash);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(token + (i * 2), "%02x", hash[i]);
    }
    token[SHA256_DIGEST_LENGTH * 2] = '\0';
}


// Đọc dữ liệu user từ file
void load_users() {
    FILE *file = fopen(DATA_FILE, "r");
    if (!file) {
        perror("Could not open user data file");
        return;
    }
    user_count = 0;
    while (fscanf(file, "%49[^,],%49[^\n]\n", users[user_count].username, users[user_count].password) == 2) {
        user_count++;
    }
    fclose(file);
}

// Ghi dữ liệu user mới vào file
void save_user(const char *username, const char *password) {
    FILE *file = fopen(DATA_FILE, "a");
    if (!file) {
        perror("Could not open user data file");
        return;
    }
    fprintf(file, "%s,%s\n", username, password);
    fclose(file);
}

// Tìm user trong danh sách
int find_user(const char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0)
            return i;
    }
    return -1;
}

void reset() {
    active_player_count = 0;
    game_in_progress = 0; // 0: Không có trò chơi, 1: Đang diễn ra
    main_player_choice = 0; // 0: Chưa chọn, 1: Chọn đúng
    number_of_questions = 0;
    eliminated_player_count = 0; // Số người chơi bị loại
    skipped_count = 0;
}

//Them o day
void handle_joingame(int client_sock, char *token) {
    char response[BUFFER_SIZE];
    char timestamp[20];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", localtime(&now));

    if (game_in_progress) {
        snprintf(response, sizeof(response), "%s|JOINGAME|The game is occurring!", timestamp);
        send(client_sock, "JOINGAME|The game is occurring!", 32, 0);
        return;
    }

    pthread_mutex_lock(&lock);
    for (int i = 0; i < active_player_count; i++) {
        if (strcmp(active_players[i].token, token) == 0) {
            pthread_mutex_unlock(&lock);
            snprintf(response, sizeof(response), "%s|JOINGAME|You are already in the game!", timestamp);
            send(client_sock, "JOINGAME|You are already in the game!", 38, 0);
            return;
        }
    }

    strcpy(active_players[active_player_count].token, token);
    active_players[active_player_count].client_sock = client_sock;
    //printf("%s\n", active_players[active_player_count].token);
    active_player_count++;
    pthread_mutex_unlock(&lock);

    snprintf(response, sizeof(response), "%s|JOINGAME|You have joined game successfully!", timestamp);
    send(client_sock, "JOINGAME|You have joined game successfully!", 44, 0);

    if(active_player_count >= MIN_PLAYERS){
        usleep(1*1000*1000);
        printf("Enough players joined, starting the game.\n");
        game_in_progress = 1;
        usleep(1*1000*1000);
        handle_game_start();
    }
}

void handle_game_start(){
    char message[BUFFER_SIZE];
    snprintf(message, sizeof(message), "STARTGAME|%d|%s|%s|%s|%s|%s", 
        number_of_questions, questions[number_of_questions].question, 
        questions[number_of_questions].answers[0], questions[number_of_questions].answers[1],
        questions[number_of_questions].answers[2], questions[number_of_questions].answers[3]);
    pthread_mutex_lock(&lock);
    for (int i = 0; i < active_player_count; i++)
    {
        active_players[i].points = 10; // Khởi tạo điểm ban đầu
        active_players[i].eliminated = 0; // Không ai bị loại ban đầu
        active_players[i].answered = 0;
        active_players[i].correct = 0;
        send(active_players[i].client_sock, message, strlen(message), 0);
    }
    pthread_mutex_unlock(&lock);
}

void handle_answer(int client_sock, const char *token, const char *answer){
    pthread_mutex_lock(&lock);
    for (int i = 0; i < active_player_count; i++) {
        if (strcmp(active_players[i].token, token) == 0) {
            if (active_players[i].answered) {
                send(client_sock, "RESULT|You have already answered!\n", 35, 0);
            } else {
                active_players[i].answered = 1;
                active_players[i].answer_time = time(NULL);
                if (strcmp(answer, questions[number_of_questions].correct_answers) == 0) {
                    active_players[i].correct = 1;
                    send(client_sock, "ANSWER|Correct!", 17, 0);
                } else {
                    send(client_sock, "ANSWER|Wrong!", 15, 0);
                }
            }
            break;
        }
    }
    pthread_mutex_unlock(&lock);
}

void determine_main_player() {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < active_player_count; i++) {
        if (active_players[i].correct) {
            if (main_player == NULL || active_players[i].answer_time < main_player->answer_time) {
                main_player = &active_players[i];
            }
        }
    }

    if (main_player) {
        char message[BUFFER_SIZE];
        snprintf(message, sizeof(message), "MAINPLAYER|%s", main_player->token);
        for (int i = 0; i < active_player_count; i++) {
            send(active_players[i].client_sock, message, strlen(message), 0);
        }
        main_player_choice = 1;
    } else {
        for (int i = 0; i < active_player_count; i++) {
            send(active_players[i].client_sock, "MAINPLAYER|No correct answer, no main player!", 46, 0);
        }
    }
    pthread_mutex_unlock(&lock);
}

void reset_game_state(){
    pthread_mutex_lock(&lock);
    for (int i = 0; i < active_player_count; i++) {
        active_players[i].answered = 0;
        active_players[i].correct = 0;
        active_players[i].answer_time = 0;
    }
    pthread_mutex_unlock(&lock);
}


// Bắt đầu vòng chơi mới
void start_new_round() {
    reset_game_state();
    number_of_questions++;

    char question_buffer[BUFFER_SIZE];
    snprintf(question_buffer, sizeof(question_buffer), 
        "QUESTION|%d|%s|%s|%s|%s|%s", number_of_questions, questions[number_of_questions].question, 
        questions[number_of_questions].answers[0], questions[number_of_questions].answers[1],
        questions[number_of_questions].answers[2], questions[number_of_questions].answers[3]);

    pthread_mutex_lock(&lock);
    for (int i = 0; i < active_player_count; i++) {
        if (!active_players[i].eliminated) {
            send(active_players[i].client_sock, question_buffer, strlen(question_buffer), 0);
        }
    }
    pthread_mutex_unlock(&lock);

    time_t round_start_time = time(NULL);
    while (1) {
        pthread_mutex_lock(&lock);
        int all_answered = 1;
        for (int i = 0; i < active_player_count; i++) {
            if (!active_players[i].eliminated && !active_players[i].answered) {
                all_answered = 0;
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        if (all_answered || difftime(time(NULL), round_start_time) >= 60) {
            break;
        }
        usleep(1 * 1000 * 1000); // Chờ 1s
    }
}

// Xử lý kết quả vòng chơi
void handle_round_result() {
    float total_wrong_points = 0;
    Player *new_main_player = NULL;
    time_t fastest_time = 0;

    // Kiểm tra xem tất cả người chơi đã trả lời chưa
    int all_players_answered = 1; // Giả sử ban đầu mọi người đã trả lời

    pthread_mutex_lock(&lock); // Đảm bảo tính đồng bộ
    for (int i = 0; i < active_player_count; i++) {
        if (!active_players[i].answered && !active_players[i].eliminated) {
            all_players_answered = 0; // Nếu có ai chưa trả lời, gán giá trị false
            break;
        }
    }
    pthread_mutex_unlock(&lock);

    // Nếu tất cả đã trả lời, tiến hành vòng tiếp theo
    if (all_players_answered) {
        usleep(1 * 1000 * 1000); // Chờ 1s
        for (int i = 0; i < active_player_count; i++) {
            if (!active_players[i].eliminated) {
                if (active_players[i].correct) {
                    if (active_players[i].client_sock != main_player->client_sock &&
                        (!new_main_player || active_players[i].answer_time < fastest_time)) {
                        new_main_player = &active_players[i];
                        fastest_time = active_players[i].answer_time;
                    }
                } else if(strcmp(active_players[i].token, main_player->token) == 0 && skipped_count <2 && strcmp(main_player_answer, "5") == 0){
                    total_wrong_points += active_players[i].points / 2;
                    active_players[i].points = active_players[i].points / 2;
                } else {
                    active_players[i].eliminated = 1;
                    total_wrong_points += active_players[i].points;
                    eliminated_player_count++;
                }
            }
        }

        //printf("\n\n%.2f\n\n", total_wrong_points);
        //printf("\nMain player anwser: \n%d\nSkip count: %d\n", strcmp(main_player_answer, "5"), skipped_count);

        if (main_player->correct) {
            main_player->points += total_wrong_points;
            //printf("1\n\n");
            //printf("End round, Main player: %s, %.2f \n", main_player->token, main_player->points);
            if(active_player_count - eliminated_player_count > 1 && number_of_questions < 9){
                usleep(1 * 1000 * 1000);
                start_new_round();
            }else{
                char message[BUFFER_SIZE];
                snprintf(message, sizeof(message), "ROUNDRESULT|MAINPLAYER_WIN|%s|%.2f", main_player->token, main_player->points);
                for (int i = 0; i < active_player_count; i++) {
                    send(active_players[i].client_sock, message, strlen(message), 0);
                }
                reset();
            }
        } else if(skipped_count < 2 && strcmp(main_player_answer, "5") == 0){
            float distributed_points = total_wrong_points / (active_player_count - eliminated_player_count - 1);
            for (int i = 0; i < active_player_count; i++) {
                if (!active_players[i].eliminated && active_players[i].correct && strcmp(active_players[i].token, main_player->token) != 0) {
                    active_players[i].points += distributed_points;
                }
            }
            //printf("\n\n2\n\n");
            //printf("End round, Main player: %s, %.2f \n", main_player->token, main_player->points);
            skipped_count++;
            usleep(1 * 1000 * 1000);
            start_new_round();
        } else {
            //printf("New main player: %s\n", new_main_player->token);
            //printf("Main player: %s\n", main_player->token);
            //printf("\n\n3\n\n");
            if (new_main_player->token) {
                float distributed_points = total_wrong_points / (active_player_count - eliminated_player_count);
                for (int i = 0; i < active_player_count; i++) {
                    if (!active_players[i].eliminated && active_players[i].correct) {
                        active_players[i].points += distributed_points;
                    }
                }
                main_player = new_main_player;
                //printf("Main player: %s, %.2f \n", main_player->token, main_player->points);
                skipped_count = 0;
                if(active_player_count - eliminated_player_count > 1 && number_of_questions < 9){
                    char message[BUFFER_SIZE];
                    snprintf(message, sizeof(message), "ROUNDRESULT|NEWMAINPLAYER|%s", main_player->token);
                    for (int i = 0; i < active_player_count; i++) {
                        if (!active_players[i].eliminated) {
                            send(active_players[i].client_sock, message, strlen(message), 0);
                        }
                    }
                    usleep(1 * 1000 * 1000);
                    start_new_round();
                }else{
                    char message[BUFFER_SIZE];
                    snprintf(message, sizeof(message), "ROUNDRESULT|MAINPLAYER_WIN|%s|%.2f", main_player->token, main_player->points);
                    for (int i = 0; i < active_player_count; i++) {
                        send(active_players[i].client_sock, message, strlen(message), 0);
                    }
                    reset();
                }
            } else {
                for (int i = 0; i < active_player_count; i++) {
                    send(active_players[i].client_sock, "ROUNDRESULT|NO_WINNER", 22, 0);
                    reset();
                }
            }
        }
    }
}

void* handle_client(void *arg) {
    ThreadArgs *args = (ThreadArgs*)arg;
    int client_sock = args->client_sock;
    free(args);

    char buffer[BUFFER_SIZE];
    char token[SHA256_DIGEST_LENGTH * 2 + 1];

    while (1) {
        bzero(buffer, sizeof(buffer));
        if (FD_ISSET(client_sock, &read_fds)) {

            int n = recv(client_sock, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                printf("Client disconnected.\n");
                close(client_sock);
                return;
            }

            printf("Received: %s\n", buffer);

            // Tách command, username và password
            char *command = strtok(buffer, " ");
            char *username = strtok(NULL, " ");
            char *password = strtok(NULL, " ");

            if (strcmp(command, "REGISTER") == 0) {
                if (find_user(username) == -1) {
                    strcpy(users[user_count].username, username);
                    strcpy(users[user_count].password, password);
                    user_count++;
                    save_user(username, password);  // Lưu vào file
                    send(client_sock, "REGISTER|Register successful!", 30, 0);
                } else {
                    send(client_sock, "REGISTER|Username is exsited. Register fail!", 45, 0);
                }
            } else if (strcmp(command, "LOGIN") == 0) {
                int index = find_user(username);
                if (index != -1 && strcmp(users[index].password, password) == 0) {
                    generate_token(token, username, client_sock);
                    printf("Generated token for %s: %s\n", username, token);
                    snprintf(buffer, sizeof(buffer), "LOGIN|Login successful!|%s", token);
                    send(client_sock, buffer, sizeof(buffer), 0);
                } else {
                    send(client_sock, "LOGIN|Account is invalid!", 26, 0);
                }
            } else if (strcmp(command, "JOINGAME") == 0) {
                char *token = username; // Token được gửi ở vị trí thứ 2
                if (token) {
                    handle_joingame(client_sock, token);
                } else {
                    send(client_sock, "JOINGAME|Invalid token\n", 24, 0);
                }
            } else if (strcmp(command, "ANSWER") == 0){
                char *token = username;
                char *questionID = password;
                char *answer = strtok(NULL, " ");
                if (token && answer){
                    handle_answer(client_sock, token, answer);
                }

                if(main_player_choice){
                    handle_round_result();
                }

                if(main_player_choice){
                    if(strcmp(main_player->token, token) == 0){
                        main_player_answer = malloc(sizeof(main_player_answer));
                        strcpy(main_player_answer, answer);
                    }
                }

            } else {
                send(client_sock, "invalid command\n", 17, 0);
            }

            if (game_in_progress && !main_player_choice){
                int all_answered = 1;
                pthread_mutex_lock(&lock);
                for(int i=0; i<active_player_count; i++){
                    if (!active_players[i].answered){
                        all_answered = 0;
                        break;
                    }
                }
                pthread_mutex_unlock(&lock);

                usleep(1 * 1000 * 1000); // Chờ 1s

                if(all_answered){
                    determine_main_player();
                }
            }

            if (game_in_progress && main_player_choice && number_of_questions == 0) {
                // Nếu tất cả đã trả lời, tiến hành vòng tiếp theo
                usleep(5 * 1000 * 1000); // Chờ 1s
                start_new_round();
            }
        }
    }
}


int main() {
    srand(time(NULL)); // Khởi tạo random seed
    load_users(); // Đọc dữ liệu từ file

    pthread_mutex_init(&lock, NULL); // Khởi tạo mutex

    int server_sock, client_sock, activity, max_sd;;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(1);
    }

    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        close(server_sock);
        exit(1);
    }

    printf("Server is running on port %d...\n", PORT);

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);
        max_sd = server_sock;

        for (int i = 0; i < client_count; i++) {
            int sd = clients[i].socket;
            FD_SET(sd, &read_fds);
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("Select error");
            break;
        }

        if (FD_ISSET(server_sock, &read_fds)) {
            client_sock = accept(server_sock, NULL, NULL);
            if (client_sock < 0) {
                perror("accept");
                continue;
            }
            clients[client_count].socket = client_sock;
            client_count++;
        }

        for (int i = 0; i < client_count; i++) {
            int sd = clients[i].socket;

            pthread_t tid;
            ThreadArgs *args = (ThreadArgs*)malloc(sizeof(ThreadArgs));
            args->client_sock = sd;
            if (pthread_create(&tid, NULL, handle_client, args) != 0) {
                perror("Could not create thread");
                close(sd);
                free(args);
            }
            pthread_detach(tid);
        }
    }

    close(server_sock);
    pthread_mutex_destroy(&lock); // Hủy mutex
    return 0;
}
