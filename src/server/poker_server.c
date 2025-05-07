#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "poker_client.h"
#include "client_action_handler.h"
#include "game_logic.h"

#define BASE_PORT 2201
#define NUM_PORTS 6
#define BUFFER_SIZE 1024

int calculate_5card_value(card_t current_hand[]);
int set_card_tie(int final_points, int ranks[]);
void sort_cards(card_t *val);
int compare_cards_by_rank_desc(const void *a, const void *b);
int get_suit(card_t card);
int get_card_rank(card_t card);
void find_next_player(game_state_t *game);

typedef struct {
    int socket;
    struct sockaddr_in address;
} player_t;

game_state_t game; //global variable to store our game state info (this is a huge hint for you)

int main(int argc, char **argv) {
    int server_fds[NUM_PORTS] = {0};
    int opt = 1;
    struct sockaddr_in server_address;
    player_t players[MAX_PLAYERS];
    char buffer[BUFFER_SIZE] = {0};
    socklen_t addrlen = sizeof(struct sockaddr_in);

    //Setup the server infrastructre and accept the 6 players on ports 2201, 2202, 2203, 2204, 2205, 2206
    for (int i = 0; i < NUM_PORTS; i++){
        if ((server_fds[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }
    
        // Set socket options
        if (setsockopt(server_fds[i], SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            perror("setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))");
            exit(EXIT_FAILURE);
        }
    
        // Bind socket to port
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = INADDR_ANY;
        server_address.sin_port = htons(BASE_PORT+i);
        if (bind(server_fds[i], (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
            perror("[Server] bind() failed.");
            exit(EXIT_FAILURE);
        }
    
        // Listen for incoming connections
        if (listen(server_fds[i], 3) < 0) {
            perror("[Server] listen() failed.");
            exit(EXIT_FAILURE);
        }
    
        printf("[Server] Running on port %d\n", BASE_PORT+i);
    }

    // Do Accept Separately after listening to all 6 ports and store the sockets:
    for (int i = 0; i < NUM_PORTS; i++){
        if ((game.sockets[i] = accept(server_fds[i], (struct sockaddr *)&server_address, (socklen_t *)&addrlen)) < 0) {
            perror("[Server] accept() failed.");
            exit(EXIT_FAILURE);
        }
    }
   
    int rand_seed = argc == 2 ? atoi(argv[1]) : 0;
    init_game_state(&game, 100, rand_seed);

    //JOIN STATE
    game.round_stage = ROUND_JOIN;
    // READ THE JOINS
    client_packet_t received_packet; // Declare a variable of the correct struct type
    memset(&received_packet, 0, sizeof(client_packet_t));


    for (int i = 0; i < MAX_PLAYERS; i++) {    
        int nbytes = read(game.sockets[i], &received_packet, sizeof(client_packet_t)); // Read JOIN into the struct
        if (nbytes <= 0) {
            fprintf(stderr, "[Server] Player %d: Read error or disconnect when expecting JOIN. Bytes: %d\n", i, nbytes);
            exit(EXIT_FAILURE);
        }
    
        if (received_packet.packet_type == JOIN) {
            printf("[Server] Player %d sent JOIN packet successfully.\n", i);
        } else {
            fprintf(stderr, "[Server] Player %d: Expected JOIN, received type %d. Protocol error.\n", i, received_packet.packet_type);
            exit(EXIT_FAILURE);
        }
    }

    while (1) {
        break;
        // INIT STATE
        // READY
        game.round_stage = ROUND_INIT;

        // Read all the READY / LEAVE
        for (int i = 0; i < MAX_PLAYERS; i++) {    
            if (game.player_status[i] == PLAYER_LEFT){
                continue;
            }

            int nbytes = read(game.sockets[i], &received_packet, sizeof(client_packet_t));
            
            if (received_packet.packet_type == READY) {
                printf("[Server] Player %d sent READY packet successfully.\n", i);
                // If READY fails because CLIENT is out of money, do nothing and mark client as LEFT (Strict)
                if (handle_client_action(&game,i,&received_packet,NULL) == -1){ // Incase READY failed because CLIENT is out of money, they will automatically get booted out.
                    printf("[Server] Player %d sent READY packet successfully but no stacks so logging them out.\n", i);
                    close(server_fds[i]);
                    close(game.sockets[i]);
                }
            } else if (received_packet.packet_type == LEAVE) { 
                printf("[Server] Player %d sent LEAVE packet successfully.\n", i);
                handle_client_action(&game,i,&received_packet,NULL);
                close(server_fds[i]);
                close(game.sockets[i]);
            }
        }

        server_ready(&game); // Ready the server, assign the dealer
        reset_game_state(&game); // Reset the game server, assign the cur_player based on dealer

        if (game.num_players == 1){
            server_packet_t server_packet;
            server_packet.packet_type = HALT;
            send(game.sockets[game.current_player], &server_packet, sizeof(server_packet_t), 0); // Sends HALT
            break;
        } else if (game.num_players < 1){
            break;
        }

        // PREFLOP STATE
        // DEAL TO PLAYERS
        // PREFLOP BETTING
        game.round_stage = ROUND_PREFLOP;
        server_deal(&game); // Deal Cards to all ACTIVE players
        
        
        // Send INFO packet to all the ACTIVE players
        for (int i = 0; i < MAX_PLAYERS; i++) {  
            if (game.player_status[i] == PLAYER_LEFT) continue;
            server_packet_t server_packet;
            build_info_packet(&game,i,&server_packet); // Builds an INFO packet for a given PID and stores it inside server packet
            ssize_t bytes_sent = send(game.sockets[i], &server_packet, sizeof(server_packet_t), 0); // Sends the INFO packet
        }

        // Calculate Active Players : 
        int activ = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) {  
            if (game.player_status[i] == PLAYER_ACTIVE) activ++;
        }

        // Expect betting response after INFO being sent out
        for (int i = 0; i < activ; i++) { // Continue to go around betting until everyone has either folded or matched the current bet
            // Get current player
            int cur_player = game.current_player;
            
            // Handle Client Request
            // Store Server Packet
            read(game.sockets[cur_player], &received_packet, sizeof(client_packet_t)); //  Read the Packet Sent 
            
            server_packet_t server_pack;

            int chk = handle_client_action(&game,cur_player,&received_packet,&server_pack);
            send(game.sockets[cur_player], &server_pack, sizeof(server_packet_t), 0); // Send NACK if invalid response, or ACK if valid
            
            if (received_packet.packet_type == FOLD) { // Treat FOLD separately because it will pass no matter what in this scenario
                // Check how many people active and all in and jump to END state if all but one folded (TODO)
                
                // If num_players > 2 then : 
                find_next_player(&game);
                // Send INFO packet to all the ACTIVE players
                for (int i = 0; i < MAX_PLAYERS; i++) {  
                    if (game.player_status[i] == PLAYER_LEFT) continue;
                    server_packet_t server_packet;
                    build_info_packet(&game,i,&server_packet); // Builds an INFO packet for a given PID and stores it inside server packet
                    ssize_t bytes_sent = send(game.sockets[i], &server_packet, sizeof(server_packet_t), 0); // Sends the INFO packet
                }
                continue;
            } 

            if (chk != -1){
                if (received_packet.packet_type == RAISE) i = 0; // Reset i if successfully raised
                find_next_player(&game);
                // Send INFO packet to all the ACTIVE players
                for (int i = 0; i < MAX_PLAYERS; i++) {  
                    if (game.player_status[i] == PLAYER_LEFT) continue;
                    server_packet_t server_packet;
                    build_info_packet(&game,i,&server_packet); // Builds an INFO packet for a given PID and stores it inside server packet
                    ssize_t bytes_sent = send(game.sockets[i], &server_packet, sizeof(server_packet_t), 0); // Sends the INFO packet
                }
                break;    
            } else {
                i -= 1;
                // Send INFO packet to all the ACTIVE players
                for (int i = 0; i < MAX_PLAYERS; i++) {  
                    if (game.player_status[i] == PLAYER_LEFT) continue;
                    server_packet_t server_packet;
                    build_info_packet(&game,i,&server_packet); // Builds an INFO packet for a given PID and stores it inside server packet
                    ssize_t bytes_sent = send(game.sockets[i], &server_packet, sizeof(server_packet_t), 0); // Sends the INFO packet
                }
            }
        }

        // FLOP STATE
        // PLACE FLOP CARDS
        // FLOP BETTING

        // TURN STATE
        // PLACE TURN CARDS
        // TURN BETTING

        // RIVER STATE
        // PLACE RIVER CARDS
        // RIVER BETTING
        
        // SHOWDOWN STATE
        // ROUND_SHOWDOWN
    }

    printf("[Server] Shutting down.\n");

    // Close all fds (you're welcome)
    for (int i = 0; i < MAX_PLAYERS; i++) {
        close(server_fds[i]);
        if (game.player_status[i] != PLAYER_LEFT) {
            close(game.sockets[i]);
        }
    }

    return 0;
}