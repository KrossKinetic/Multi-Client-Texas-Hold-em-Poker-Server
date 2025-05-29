# CSE 220 HW5: Multi-Client Texas Hold'em Poker Server

This final project for CSE 220: Systems Fundamentals I is a server application that manages a six-player game of Texas Hold'em poker. The server is written in C and uses the sockets API to communicate with multiple clients concurrently. It is responsible for managing the entire game lifecycle, including dealing cards, handling player actions, tracking bets, and determining the winner of each hand.

---

## 🎓 Learning Outcomes

This project was designed to demonstrate and reinforce several key skills:
* Using the C sockets API to exchange messages between processes.
* Maintaining complex state in a server application with multiple clients.
* Implementing a complex comparison operation for poker hand evaluation.
* Reinforcing skills in testing and debugging a networked application.

---

## 🃏 Core Features & Game Logic

The server application emulates a full game of Texas Hold'em, handling game state, player communication, and all underlying rules.

### Server Architecture
The server listens for connections on six distinct ports, allowing up to six players to join a game. It uses the C sockets API to manage these connections, sending game state updates and receiving player actions. A key challenge is managing the state for all players, including their chip stacks, cards, current bets, and status (active, folded, or left the game).

### Texas Hold'em Game Flow
The server correctly implements the standard phases of a Texas Hold'em hand:
1.  **Pre-Flop:** Each active player is dealt two private "hole cards." A round of betting follows.
2.  **The Flop:** Three community cards are dealt face-up on the table. A second round of betting occurs.
3.  **The Turn:** A fourth community card is dealt. A third round of betting occurs.
4.  **The River:** A fifth and final community card is dealt. The final round of betting occurs.
5.  **Showdown:** If two or more players remain, they reveal their hands. The server determines the winner and awards them the pot. The hand can also end early if all players but one fold.

### Hand Comparison
A major component of this project was implementing the complex logic to compare poker hands at showdown. The server can identify the best five-card hand for each player using their two hole cards and the five community cards.

The hand evaluation logic correctly ranks all standard poker hands:
* Straight Flush
* Four of a Kind
* Full House
* Flush
* Straight
* Three of a Kind
* Two Pair
* One Pair
* High Card

The implementation also correctly handles all tie-breaker scenarios (e.g., comparing kickers for flushes or high cards, or comparing the rank of pairs in a two-pair hand).

---

## 🚀 Building and Testing

The project was compiled using a `Makefile`. Testing was a critical part of the development process and was facilitated by two provided client programs:
* **TUI Client:** A graphical terminal client that allowed for interactive play and manual testing of the server's functionality.
* **Automated Client:** A script-based client used to run automated test cases, ensuring the server's responses and state transitions were precise and correct under various scenarios.

Logs were generated for each client session, which were essential for debugging the complex packet exchanges between the server and clients.

## CSE220 Completion Score: 100%, Class Average for HW5: 35%
