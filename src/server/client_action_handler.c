#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "client_action_handler.h"
#include "game_logic.h"

/**
 * @brief Processes packet from client and generates a server response packet.
 * 
 * If the action is valid, a SERVER_INFO packet will be generated containing the updated game state.
 * If the action is invalid or out of turn, a SERVER_NACK packet is returned with an optional error message.
 * 
 * @param pid The ID of the client/player who sent the packet.
 * @param in Pointer to the client_packet_t received from the client.
 * @param out Pointer to a server_packet_t that will be filled with the response.
 * @return 0 if successful processing, -1 on NACK or error.
 */
int handle_client_action(game_state_t *game, player_id_t pid, const client_packet_t *in, server_packet_t *out) {
    // Look at all the different client packets, check current_player, see if pid matches current player and let them perform tasks

    if (game->player_status[pid] == PLAYER_FOLDED && game->round_stage != ROUND_INIT && (in->packet_type != READY && in->packet_type != LEAVE)){
        // A player who folded cannot perform any other tasks except READY/LEAVE at INIT state
        out->packet_type = NACK;
        return -1;
    }

    // Check if player wants to JOIN
    if (in->packet_type == READY){
        // Check if game is at START
        if (!(game->round_stage == ROUND_INIT)){
            if (out != NULL) out->packet_type = NACK;
            return -1;
        } 

        // Check if Player has stacks to play
        if (game->player_stacks[pid] <= 0){
            if (out != NULL) out->packet_type = NACK;
            game->player_status[pid] = PLAYER_LEFT;
            return -1;
        }

        game->player_status[pid] = PLAYER_ACTIVE; // Player is active and ready to be dealt cards
        if (out != NULL) out->packet_type = ACK;
        return 0;
    } else if (in->packet_type == LEAVE){
        // Check if game is at START, or that it is AFTER END (After END is START)
        if (!(game->round_stage == ROUND_INIT)){
            if (out != NULL) out->packet_type = NACK;
            return -1;
        } 

        game->player_status[pid] = PLAYER_LEFT; // Player has left 
        game->player_stacks[pid] = 0;
        if (out != NULL) out->packet_type = ACK;
        return 0;
    } else if (in->packet_type == RAISE){
        // Raise the bet by allowing it IF : (The bet you placed before in the round + raise amount) > highest bet
        // Raise if in PREFLOP, FLOP, RIVER, TURN (4 bets)
        if ((game->round_stage == ROUND_PREFLOP || game->round_stage == ROUND_FLOP || game->round_stage == ROUND_RIVER || game->round_stage == ROUND_TURN) && (game->current_player == pid) && (in->params[0] > 0)){
            if ((game->current_bets[pid] + in->params[0]) > game->highest_bet && (in->params[0] <= game->player_stacks[pid])){
                int new_player_total_bet_this_round = game->current_bets[pid] + in->params[0];
                game->current_bets[pid] = new_player_total_bet_this_round; // Update new bet
                game->player_stacks[pid] -= in->params[0]; // Subtract the amount raising by
                game->pot_size += in->params[0]; // Add money to the pot
                game->highest_bet = new_player_total_bet_this_round; // Update highest bets
                out->packet_type = ACK;
                return 0;
            } else {out->packet_type = NACK;return -1;}
        } else {out->packet_type = NACK; return -1;}

    } else if (in->packet_type == CALL) {
        if (game->current_player == pid && (game->round_stage == ROUND_PREFLOP || game->round_stage == ROUND_FLOP || game->round_stage == ROUND_RIVER || game->round_stage == ROUND_TURN) && (game->highest_bet > 0)){
            int amount_to_add = game->highest_bet - game->current_bets[pid]; // amount to add is the amount he needs to add on top of already bet amount to meet the highest bet amount

            if (amount_to_add > game->player_stacks[pid] || amount_to_add <= 0){
                out->packet_type = NACK;return -1;
            }

            if (amount_to_add == game->player_stacks[pid]){
                game->player_status[pid] = PLAYER_ALLIN;
            }

            game->current_bets[pid] += amount_to_add; // Add highest bet to the current player bit 
            game->player_stacks[pid] -= amount_to_add; // Subtract the highest bet from the current player stacks
            game->pot_size += amount_to_add; // Add money to the pot
            out->packet_type = ACK;
            return 0;
        } else {out->packet_type = NACK;return -1;}
    } else if (in->packet_type == CHECK){
        if (game->current_player == pid && (game->round_stage == ROUND_PREFLOP || game->round_stage == ROUND_FLOP || game->round_stage == ROUND_RIVER || game->round_stage == ROUND_TURN) && (game->highest_bet == 0)){
            // No Bets Raised, do nothing
            out->packet_type = ACK;
            return 0;
        } else {out->packet_type = NACK;return -1;}
    } else if (in->packet_type == FOLD){
        if (game->current_player == pid && (game->round_stage == ROUND_PREFLOP || game->round_stage == ROUND_FLOP || game->round_stage == ROUND_RIVER || game->round_stage == ROUND_TURN)){
            game->player_status[pid] = PLAYER_FOLDED;
            out->packet_type = ACK;
            return 0;
        } else {out->packet_type = NACK;return -1;}
    } else {
        out->packet_type = NACK;return -1;
    }
}

void build_info_packet(game_state_t *game, player_id_t pid, server_packet_t *out) {
    out->packet_type = INFO;

    out->info.pot_size = game->pot_size;
    out->info.dealer = game->dealer_player;
    out->info.player_turn = game->current_player;
    out->info.bet_size = game->highest_bet;
    memcpy(out->info.player_stacks, game->player_stacks, sizeof(int) * MAX_PLAYERS);
    memcpy(out->info.player_bets, game->current_bets, sizeof(int) * MAX_PLAYERS);
    memcpy(out->info.player_cards, game->player_hands[pid], sizeof(card_t) * HAND_SIZE);
    memcpy(out->info.community_cards, game->community_cards, sizeof(card_t) * MAX_COMMUNITY_CARDS);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        switch (game->player_status[i]) {
            case PLAYER_ACTIVE:
            case PLAYER_ALLIN:
                out->info.player_status[i] = 1;
                break;
            case PLAYER_FOLDED:
                out->info.player_status[i] = 0;
                break;
            case PLAYER_LEFT:
            default:
                out->info.player_status[i] = 2;
                break;
        }
    }
}

void build_end_packet(game_state_t *game, player_id_t winner, server_packet_t *out) {
    out->packet_type = END;
    end_packet_t *end_info = &(out->end);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->player_status[i] != PLAYER_LEFT) {
            memcpy(end_info->player_cards[i], game->player_hands[i], sizeof(card_t) * HAND_SIZE);
        } else {
            // For players who left, their cards are not relevant or shown
            end_info->player_cards[i][0] = NOCARD;
            end_info->player_cards[i][1] = NOCARD;
        }
    }

    memcpy(end_info->community_cards, game->community_cards, sizeof(card_t) * MAX_COMMUNITY_CARDS);
    memcpy(end_info->player_stacks, game->player_stacks, sizeof(int) * MAX_PLAYERS);
    end_info->pot_size = game->pot_size;
    end_info->dealer = game->dealer_player;
    end_info->winner = winner;

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        switch (game->player_status[i]) {
            case PLAYER_ACTIVE:
            case PLAYER_ALLIN:
                end_info->player_status[i] = 1;
                break;
            case PLAYER_FOLDED:
                end_info->player_status[i] = 0;
                break;
            case PLAYER_LEFT:
            default:
                end_info->player_status[i] = 2;
                break;
        }
    }
}
