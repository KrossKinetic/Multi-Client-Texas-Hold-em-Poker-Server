#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "poker_client.h"
#include "client_action_handler.h"
#include "game_logic.h"

int calculate_5card_value(card_t current_hand[]);
int set_card_tie(int final_points, int ranks[]);
void sort_cards(card_t *val);
int compare_cards_by_rank_desc(const void *a, const void *b);
int get_suit(card_t card);
int get_card_rank(card_t card);
void find_next_player(game_state_t *game, int flag);

//Feel free to add your own code. I stripped out most of our solution functions but I left some "breadcrumbs" for anyone lost
void init_deck(card_t deck[DECK_SIZE], int seed){ //DO NOT TOUCH THIS FUNCTION
    srand(seed);
    int i = 0;
    for(int r = 0; r<13; r++){
        for(int s = 0; s<4; s++){
            deck[i++] = (r << SUITE_BITS) | s;
        }
    }
}

void shuffle_deck(card_t deck[DECK_SIZE]){ //DO NOT TOUCH THIS FUNCTION
    for(int i = 0; i<DECK_SIZE; i++){
        int j = rand() % DECK_SIZE;
        card_t temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
}

//You dont need to use this if you dont want, but we did.
void init_game_state(game_state_t *game, int starting_stack, int random_seed){
    memset(game, 0, sizeof(game_state_t));
    init_deck(game->deck, random_seed);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        game->player_stacks[i] = starting_stack;
    }
}

/*
 * Resets the game state between hands to prepare for the next deal.
 * This function assumes that:
 * - The previous hand's winner has been determined and pot awarded (player_stacks updated).
 * - READY/LEAVE statuses have been processed for all players.
 * - Final player eligibility (player_status: ACTIVE/LEFT/BUSTED) for the *next* hand is set.
 * - The dealer position (dealer_player) for the *next* hand has been set.
 *
 * What this function DOES reset:
 * - Shuffles the deck array.
 * - Clears player_hands (sets cards to NOCARD).
 * - Clears community_cards (sets cards to NOCARD).
 * - Resets next_card index to 0.
 * - Resets current_bets for all players to 0.
 * - Resets highest_bet to 0.
 * - Resets pot_size to 0.
 * - Sets round_stage to ROUND_PREFLOP.
 * - Calculates and sets the initial current_player for the new hand.
 *
 * What this function does NOT handle (should be done *before* calling this):
 * - Updating player_stacks with wins/losses from the previous hand.
 * - Processing network packets for READY/LEAVE signals.
 * - Determining final player_status based on READY/LEAVE and chip counts.
 * - Rotating/setting the dealer_player for the new hand.
 * - Checking for the HALT condition (< 2 active players).
 */
void reset_game_state(game_state_t *game) {
    shuffle_deck(game->deck);
    
    // Resetting cards that the players have
    for (int i = 0; i < MAX_PLAYERS; i++){
        for (int j = 0; j < HAND_SIZE; j++){
            game->player_hands[i][j] = NOCARD;
        }
    }

    // Resetting community cards
    for (int i = 0; i < MAX_COMMUNITY_CARDS; i++){
        game->community_cards[i] = NOCARD;
    }

    // Resetting Next Card to be 0
    game->next_card = 0;

    // Resetting bets
    for (int i = 0; i < MAX_PLAYERS; i++){
        game->current_bets[i] = 0;
    }

    // Resetting highest bet
    game->highest_bet = 0;

    // Reset the pot
    game->pot_size = 0;

    // Set round stage to init
    game->round_stage = ROUND_INIT;

    // Reset current_player
    int cur_player = game->dealer_player;
    for (int i = 0; i < MAX_PLAYERS; i++){
        cur_player =  (cur_player+1)%MAX_PLAYERS;
        if (game->player_status[cur_player] == PLAYER_ACTIVE) break;
    }
    game->current_player = cur_player;
}

/*
 * Determines player eligibility for the next hand, checks if the game can continue,
 * and assigns the dealer for the upcoming hand.
 *
 * !!! CRUCIAL ASSUMPTION !!!
 * This function relies on the network handling code (run *before* this)
 * having already set game->player_status[i] = PLAYER_LEFT for any player 'i'
 * who sent a LEAVE packet OR who timed out / did not send a READY packet
 * during the inter-hand waiting period. Status is assumed to be left unchanged
 * by the network code only if READY was successfully received.
 *
 * Actions performed within this function:
 * 1. Finalizes Player Status: Iterates through all players:
 * - If a player has chips (stack > 0) AND their status was not already set
 * to PLAYER_LEFT by the network code (implying READY was received),
 * their status is set to PLAYER_ACTIVE for the next hand.
 * - Otherwise (no chips OR already marked PLAYER_LEFT), their status is
 * confirmed or set to PLAYER_LEFT.
 * 2. Counts Active Players: Tallies the number of players successfully set to PLAYER_ACTIVE.
 * 3. Checks Halt Condition: If the active player count is less than 2, returns 0 (signal to HALT).
 * 4. Assigns New Dealer: If the game proceeds (count >= 2), finds the first
 * PLAYER_ACTIVE player clockwise starting after the *previous* dealer. Updates
 * game->dealer_player to this new dealer's index.
 * 5. Returns Proceed Signal: If the game proceeds, returns 1.
 */
int server_ready(game_state_t *game) {
    //This function updated the dealer and checked ready/leave status for all players
    // Update Ready / Leave status for all players
    game->num_players = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->player_stacks[i] <= 0) {
            game->player_status[i] = PLAYER_LEFT;
        }

        if (game->player_status[i] == PLAYER_ACTIVE){
            game->num_players++;
        }
    }

    if (game->num_players == 1){
        return 0; // Return 0 not enuf players
    }

    if (game->num_players == 0){
        return -1; // Return 0 not enuf players
    }

    int cur_player = game->dealer_player;
    for (int i = 0; i < MAX_PLAYERS; i++){
        cur_player =  (cur_player+1)%MAX_PLAYERS;
        if (game->player_status[cur_player] == PLAYER_ACTIVE) break;
    }
    game->dealer_player = cur_player;

    return 1;
}

//This was our dealing function with some of the code removed (I left the dealing so we have the same logic)
void server_deal(game_state_t *game) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->player_status[i] == PLAYER_ACTIVE) {
            game->player_hands[i][0] = game->deck[game->next_card++];
            game->player_hands[i][1] = game->deck[game->next_card++];
        }
    }
}

/*
 * Determines if the current betting round has concluded.
 * Checks two conditions:
 * 1. If fewer than two players remain active in the hand (status PLAYER_ACTIVE or PLAYER_ALLIN).
 * 2. If all players who are still able to act (status PLAYER_ACTIVE) have
 * matched the current highest bet for this round (game->current_bets[i] >= game->highest_bet).
 * (Players who are PLAYER_ALLIN are considered to have completed their action).
 *
 * This check should be performed after each player action to see if the round
 * should transition to the next stage (e.g., dealing next community card or showdown).
 *
 * @param game Pointer to the current game state.
 * @return int - Returns 1 if the betting round is finished, 0 if it needs to continue.
 */
int server_bet(game_state_t *game) {
    //This was our function to determine if everyone has called or folded

    int player_count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++){
        if (game->player_status[i] == PLAYER_ACTIVE || game->player_status[i] == PLAYER_ALLIN){
            player_count++;
        }
    }

    if (player_count < 2){
        // If 1 or 0 player left, round is over
        return 1;
    }

    for (int i = 0; i < MAX_PLAYERS; i++){
        if (game->player_status[i] == PLAYER_ACTIVE){
            if (game->current_bets[i] < game->highest_bet){
                return 0;
            }
        }
    }

    return 1;
}

/*
 * Deals the appropriate community cards based on the current game stage.
 * This function is intended to be called *after* the previous betting round
 * has concluded and the game->round_stage has been updated to the stage
 * for which cards should now be dealt (e.g., ROUND_FLOP, ROUND_TURN, or ROUND_RIVER).
 *
 * - If the current stage is ROUND_FLOP, it deals 3 cards.
 * - If the current stage is ROUND_TURN, it deals the 4th card.
 * - If the current stage is ROUND_RIVER, it deals the 5th card.
 *
 * It deals cards from the game->deck using and incrementing game->next_card.
 * Includes basic safety checks to prevent dealing beyond the deck size.
 *
 * @param game Pointer to the game_state_t struct, which will be modified
 * (community_cards and next_card fields).
 */
void server_community(game_state_t *game) {
    round_stage_t round_stage = game->round_stage;
    if (round_stage == ROUND_FLOP) {
        // We are now IN the FLOP stage, deal the 3 flop cards
        if (game->next_card + 3 <= DECK_SIZE) {
            game->community_cards[0] = game->deck[game->next_card++];
            game->community_cards[1] = game->deck[game->next_card++];
            game->community_cards[2] = game->deck[game->next_card++];
        } else { printf("Out of Deck Cards"); }
    } else if (round_stage == ROUND_TURN) {
        // We are now IN the TURN stage, deal the 1 turn card
         if (game->next_card + 1 <= DECK_SIZE) {
            game->community_cards[3] = game->deck[game->next_card++];
         } else { printf("Out of Deck Cards"); }
    } else if (round_stage == ROUND_RIVER) {
        // We are now IN the RIVER stage, deal the 1 river card
         if (game->next_card + 1 <= DECK_SIZE) {
            game->community_cards[4] = game->deck[game->next_card++];
         } else { printf("Out of Deck Cards"); }
    }
}

// Finds the next available player
void find_next_player(game_state_t *game, int flag){
    int cur_player;

    if (flag == 1) cur_player = game->dealer_player;
    else cur_player = game->current_player;

    for (int i = 0; i < MAX_PLAYERS; i++){
        cur_player = (cur_player+1)%MAX_PLAYERS;
        if (game->player_status[cur_player] == PLAYER_ACTIVE) break;
    }
    game->current_player = cur_player;
}

// Returns the rank of the card
int get_card_rank(card_t card){
    return (card >> 2) + 2;
}

// Returns the suit of the card
int get_suit(card_t card) {
    return card & 3; 
}

// Compare Poker Ranks
int compare_cards_by_rank_desc(const void *a, const void *b) {
    int rank_a = get_card_rank(*((const card_t *)a));
    int rank_b = get_card_rank(*((const card_t *)b));

    if (rank_a > rank_b) {
        return -1; // rank_a is higher
    } else if (rank_a < rank_b) {
        return 1;  // rank_a is lower
    } else {
        return 0;  // Ranks are equal
    }
}

// Sorts the hand in descending order
void sort_cards(card_t *val){
    qsort(val,7,sizeof(val[0]),compare_cards_by_rank_desc);
}

// Sets all the cards in the hand to the points tiebreaker bits
int set_card_tie(int final_points, int ranks[]){
    final_points |= ranks[0] << 16; // Setting card ranks as tiebreakers
    final_points |= ranks[1] << 12;
    final_points |= ranks[2] << 8;
    final_points |= ranks[3] << 4;
    final_points |= ranks[4];
    return final_points;
}

// Calculates the "value" of single 5 card hand
int calculate_5card_value(card_t current_hand[]){
    // Perform check of hands on the 5 cards
    int final_points = 0; // Point Description: [HAND_RANK]20 [Tie1]16 [Tie2]12 [Tie3]8 [Tie4]4 [Tie5]0;

    card_t ranks[5] = {get_card_rank(current_hand[0]),get_card_rank(current_hand[1]),get_card_rank(current_hand[2]),get_card_rank(current_hand[3]),get_card_rank(current_hand[4])};
    card_t suits[5] = {get_suit(current_hand[0]),get_suit(current_hand[1]),get_suit(current_hand[2]),get_suit(current_hand[3]),get_suit(current_hand[4])};

    // Check if Straight Flush
    if ((ranks[0] == (ranks[1]+1) && // Check if the cards are consecutive
        ranks[1] == (ranks[2]+1) && 
        ranks[2] == (ranks[3]+1) &&
        ranks[3] == (ranks[4]+1)) && 
        (suits[0] == suits[1] && // Check if the cards are same suit
        suits[1] == suits[2] &&
        suits[2] == suits[3] && 
        suits[3] == suits[4])){

        final_points = 9 << 20; // Setting [Hand Rank]20 to 9
        final_points |= ranks[0] << 16; 
        return final_points;
    } else if ((ranks[0] == 14 && ranks[1] == 5 && ranks[2] == 4 &&
        ranks[3] == 3 && ranks[4] == 2) &&
        (suits[0] == suits[1] && suits[1] == suits[2] &&
        suits[2] == suits[3] && suits[3] == suits[4])){
        
        final_points = 9 << 20; // Setting [Hand Rank]20 to 9
        final_points |= 5 << 16; 
        return final_points;
    }    
    else if ((ranks[0] == ranks[1] && ranks[1] == ranks[2] && ranks[2] == ranks[3])){
        // 4 of a kind of type: AAAAB
        final_points = 8 << 20; // Setting [Hand Rank]20 to 8
        final_points |= ranks[0] << 16;
        return final_points;
    } else if (ranks[3] == ranks[4] && ranks[1] == ranks[2] && ranks[2] == ranks[3]){
        // 4 of a kind of type: ABBBB
        final_points = 8 << 20; // Setting [Hand Rank]20 to 8
        final_points |= ranks[1] << 16;
        return final_points;
    } else if (ranks[0] == ranks[1] && ranks[1] == ranks[2] && ranks[3] == ranks[4]){
        // Full house BBBAA
        final_points = 7 << 20; // Setting [Hand Rank]20 to 7
        final_points |= ranks[0] << 16;
        final_points |= ranks[3] << 12;
        return final_points;
    } else if (ranks[0] == ranks[1] && ranks[2] == ranks[3] && ranks[3] == ranks[4]){
        // Full house AABBB
        final_points = 7 << 20; // Setting [Hand Rank]20 to 7
        final_points |= ranks[2] << 16;
        final_points |= ranks[0] << 12;
        return final_points;
    } else if (suits[0] == suits[1] && // Check if the cards are same suit
        suits[1] == suits[2] &&
        suits[2] == suits[3] && 
        suits[3] == suits[4]){
        // Flush
        final_points = 6 << 20; // Setting [Hand Rank]20 to 6
        final_points = set_card_tie(final_points,ranks); // Add all card ranks to compare the ones that are different and ignore the common ones
        return final_points;
    } else if (ranks[0] == (ranks[1]+1) &&
        ranks[1] == (ranks[2]+1) && 
        ranks[2] == (ranks[3]+1) &&
        ranks[3] == (ranks[4]+1)){
        // Straight
        final_points = 5 << 20; // Setting [Hand Rank]20 to 5
        final_points |= ranks[0] << 16;
        return final_points;
    } else if (ranks[0] == 14 &&
        ranks[1] == 5 && 
        ranks[2] == 4 &&
        ranks[3] == 3 &&
        ranks[4] == 2) {
        // Special A-5-4-3-2 Case for Straights
        final_points = 5 << 20; // Setting [Hand Rank]20 to 5
        final_points |= ranks[1] << 16;
        return final_points;
    } else if (ranks[0] == ranks[1] && ranks[1] == ranks[2]){
        // Three of a kind, AAABC
        final_points = 4 << 20; // Setting [Hand Rank]20 to 4
        final_points |= ranks[0] << 16;
        return final_points;
    } else if (ranks[1] == ranks[2] && ranks[2] == ranks[3]){
        // Three of a kind, ABBBC
        final_points = 4 << 20; // Setting [Hand Rank]20 to 4
        final_points |= ranks[1] << 16;
        return final_points;
    } else if (ranks[2] == ranks[3] && ranks[3] == ranks[4]){
        // Three of a kind, ABCCC
        final_points |= 4 << 20; // Setting [Hand Rank]20 to 4
        final_points |= ranks[2] << 16;
        return final_points;
    }  else if (ranks[0] == ranks[1] && ranks[2] == ranks[3]){
        // Two Pair AABBC
        final_points = 3 << 20; // Setting [Hand Rank]20 to 3
        final_points |= ranks[0] << 16;
        final_points |= ranks[2] << 12;
        return final_points;
    } else if (ranks[0] == ranks[1] && ranks[3] == ranks[4]){
        // Two pair AABCC
        final_points = 3 << 20; // Setting [Hand Rank]20 to 3
        final_points |= ranks[0] << 16;
        final_points |= ranks[3] << 12;
        return final_points;
    } else if (ranks[1] == ranks[2] && ranks[3] == ranks[4]){
        // Two Pair ABBCC
        final_points = 3 << 20; // Setting [Hand Rank]20 to 3
        final_points |= ranks[1] << 16;
        final_points |= ranks[3] << 12;
        return final_points;
    } else if (ranks[0] == ranks[1]){
        // One Pair AABCD
        final_points = 2 << 20; // Setting [Hand Rank]20 to 2
        final_points |= ranks[0] << 16;
        return final_points;
    } else if (ranks[1] == ranks[2]){
        // One Pair ABBCD
        final_points = 2 << 20; // Setting [Hand Rank]20 to 2
        final_points |= ranks[1] << 16;
        return final_points;
    } else if (ranks[2] == ranks[3]){
        // One Pair ABCCD
        final_points = 2 << 20; // Setting [Hand Rank]20 to 2
        final_points |= ranks[2] << 16;
        return final_points;
    } else if (ranks[3] == ranks[4]){
        // One Pair ABCDD
        final_points = 2 << 20; // Setting [Hand Rank]20 to 2
        final_points |= ranks[3] << 16;
        return final_points;
    } else {
        // High Card 
        final_points = 1 << 20; 
        final_points = set_card_tie(final_points,ranks);  // Add all card ranks to compare the ones that are different and ignore the common ones
        return final_points;
    }
}

// Function to evaluate the value of each player's hand
int evaluate_hand(game_state_t *game, player_id_t pid) {
    card_t all_cards[7];

    all_cards[0] = game->player_hands[pid][0];
    all_cards[1] = game->player_hands[pid][1];

    for (int i = 0; i < 5; ++i) {
        all_cards[2 + i] = game->community_cards[i];
    }

    sort_cards(all_cards);

    card_t current_hand[5] = {0};
    int max_value = 0;
    
    // 5 Loops shouldn't be stacked for efficiency, but here its O(1) and small loop ALWAYS so its fine
    for (int c1 = 0; c1 <= 2; c1++) {
        for (int c2 = c1 + 1; c2 <= 3; c2++) {
            for (int c3 = c2 + 1; c3 <= 4; c3++) {
                for (int c4 = c3 + 1; c4 <= 5; c4++) {
                    for (int c5 = c4 + 1; c5 <= 6; c5++) {
                        // We have a unique combination of 5 indices: c1, c2, c3, c4, c5
                        current_hand[0] = all_cards[c1];
                        current_hand[1] = all_cards[c2];
                        current_hand[2] = all_cards[c3];
                        current_hand[3] = all_cards[c4];
                        current_hand[4] = all_cards[c5];

                        int current_value = calculate_5card_value(current_hand); // Your function from Step 3

                        if (current_value > max_value) {
                            max_value = current_value;
                        }
                    }
                }
            }
        }
    }

    return max_value;
}

// Returns the pid of the winner
int find_winner(game_state_t *game) {
    player_id_t winning_player_id = -1;
    int highest_hand_value = -1;
    
    for (player_id_t i = 0; i < MAX_PLAYERS; i++) {
        if (game->player_status[i] == PLAYER_ACTIVE || game->player_status[i] == PLAYER_ALLIN) {

            // Evaluate this player's best 5-card hand
            int current_player_hand_value = evaluate_hand(game, i);

            if (current_player_hand_value > highest_hand_value) {
                highest_hand_value = current_player_hand_value;
                winning_player_id = i; // This player is the new winner
            }
        }
    }
    
    return winning_player_id;
}

void broadcast_info(game_state_t *game) {
    server_packet_t server_packet;
    for (int i = 0; i < MAX_PLAYERS; i++) {  
        if (game->player_status[i] == PLAYER_LEFT) continue;
        build_info_packet(game,i,&server_packet); // Builds an INFO packet for a given PID and stores it inside server packet
        send(game->sockets[i], &server_packet, sizeof(server_packet_t), 0); 
    }
}

void broadcast_end(game_state_t *game, int pid) {
    server_packet_t server_packet;
    for (int i = 0; i < MAX_PLAYERS; i++) {  
        if (game->player_status[i] == PLAYER_LEFT) continue;
        build_end_packet(game,pid,&server_packet); // Builds an END packet for a given PID and stores it inside server packet
        ssize_t bytes_sent = send(game->sockets[i], &server_packet, sizeof(server_packet_t), 0); // Sends the END packet
    }
}

// Does betting until everyone had a chance to RAISE
int do_betting(game_state_t *game, client_packet_t *received_packet){
    int activ = 0;
    int all = 0;
    int folded = 0;
    int left = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {  
        if (game->player_status[i] == PLAYER_ACTIVE) activ++;
        if (game->player_status[i] == PLAYER_ALLIN ) all++;
        if (game->player_status[i] == PLAYER_FOLDED) folded++;
        if (game->player_status[i] == PLAYER_LEFT) left++;
    }

    int activ_all = activ + all;
    int activ_all_temp = activ;
    int activ_all_temp2 = activ;

    // Expect betting response after INFO being sent out
    for (int i = 0; i < activ_all_temp; i++) { // Continue to go around betting until everyone has either folded or matched the current bet
        // Get current player

        int cur_player = game->current_player;
        
        printf("[Server] Betting for Player: %d \n",game->current_player);
        
        read(game->sockets[cur_player], received_packet, sizeof(client_packet_t)); //  Read the Packet Sent 
        
        server_packet_t server_pack;
        
        int chk = handle_client_action(game,cur_player,received_packet,&server_pack);

        send(game->sockets[cur_player],&server_pack, sizeof(server_packet_t), 0);

        if (received_packet->packet_type == FOLD) { // Treat FOLD separately because it will pass no matter what in this scenario
            activ--; // One active player folded
            activ_all_temp2--;

            if (activ < 2){ // If all except 1 folded, jump to end state
                return 1; // return 1 if isEnd
            }
        } 

        if (chk == 0){ // IF ACK
            printf("[Server] Client Handler Betting for Player: %d was a SUCCESS \n",game->current_player);
            if (received_packet->packet_type == RAISE) {i = 0;activ_all_temp=activ_all_temp2;} // Reset i if successfully raised
        
            if ((i + 1) != activ_all_temp) find_next_player(game,0);
            else find_next_player(game,1);

            if ((i + 1) != activ_all_temp) broadcast_info(game); // Only broadcast if it is NOT the last iter.
        } else { // IF NACK
            printf("[Server] Client Handler Betting for Player: %d was FAILED \n",game->current_player);
            i -= 1;        
        }
    }
    return 0; // Betting successful
}