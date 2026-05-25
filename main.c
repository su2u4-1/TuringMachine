#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

typedef struct {
    struct {
        size_t next;
        char read;
        char write;
        char move;
    }* actions;
    size_t now_state;
    char action_count;
} Card;

static Card* cards = NULL;
static size_t card_count = 0;

typedef struct Tape Tape;
struct Tape {
    char* data;
    Tape* left_bound;
    Tape* right_bound;
};
#define tape_size 2147483647
static size_t tape_count = 0;

static Tape* head_tape = 0;
static int head_pos = 0;

static void init(char* card_file, char* tape_file, int tape_mode);

static void create_tapes(int is_left, Tape* now_tape);
static void read_initial_tape(char* tape_file, int mode);
static void free_tapes(void);
static void read_cards(char* card_file);
static void free_cards(void);

static char write_move_and_read(int is_left, char write_value);
noreturn static void allocate_error(char* function_name);

int main(int argc, char* argv[]) {
    return 0;
}

void init(char* card_file, char* tape_file, int tape_mode) {
    ++tape_count;
    head_tape = (Tape*)malloc(sizeof(Tape));
    if (head_tape == NULL) allocate_error("init 0");
    head_tape->data = (char*)malloc(sizeof(char) * tape_size);
    if (head_tape->data == NULL) allocate_error("init 1");
    head_tape->left_bound = NULL;
    head_tape->right_bound = NULL;
    read_initial_tape(tape_file, tape_mode);
    read_cards(card_file);
}

static void create_tapes(int is_left, Tape* now_tape) {
    ++tape_count;
    Tape* new_tape = (Tape*)malloc(sizeof(Tape));
    if (new_tape == NULL) allocate_error("create_tapes 0");
    new_tape->data = (char*)malloc(sizeof(char) * tape_size);
    if (new_tape->data == NULL) allocate_error("create_tapes 1");
    assert((is_left && now_tape->right_bound == NULL) || (!is_left && now_tape->left_bound == NULL));
    if (is_left) {
        now_tape->left_bound = new_tape;
        new_tape->right_bound = now_tape;
        new_tape->left_bound = NULL;
    } else {
        now_tape->right_bound = new_tape;
        new_tape->left_bound = now_tape;
        new_tape->right_bound = NULL;
    }
}

void read_initial_tape(char* tape_file, int mode) {
    FILE* file = NULL;
    if (mode)
        file = fopen(tape_file, "r");
    else
        file = fopen(tape_file, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to open tape file: %s\n", tape_file);
        exit(EXIT_FAILURE);
    }
    size_t index = 0;
    while (index < tape_size) {
        char value;
        if (mode) {
            int check = fscanf(file, " %*[, \t\r\n\f\v]%hhd%*[, \t\r\n\f\v]", &value);
            if (check != 1) break;
        } else {
            size_t check = fread(&value, sizeof(char), 1, file);
            if (check != 1) break;
        }
        head_tape->data[index++] = value;
    }
}

static void free_tapes(void) {
    Tape* current = head_tape;
    while (current->left_bound != NULL)
        current = current->left_bound;
    while (current != NULL) {
        Tape* next = current->right_bound;
        free(current->data);
        free(current);
        current = next;
    }
}

static void read_cards(char* card_file) {
    FILE* file = fopen(card_file, "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open card file: %s\n", card_file);
        exit(EXIT_FAILURE);
    }
    int check = fscanf(file, "%zu", &card_count);
    if (check == EOF || check != 1) {
        fprintf(stderr, "Failed to read card count from file: %s\n", card_file);
        exit(EXIT_FAILURE);
    }
    cards = malloc(sizeof(Card) * card_count);
    if (cards == NULL) allocate_error("read_cards 0");
    for (size_t i = 0; i < card_count; ++i) {
        Card* card = &cards[i];
        check = fscanf(file, "%zu %hhd", &card->now_state, &card->action_count);
        if (check == EOF || check != 2) {
            fprintf(stderr, "Failed to read card %zu from file: %s\n", i, card_file);
            exit(EXIT_FAILURE);
        }
        card->actions = malloc(sizeof(*card->actions) * (size_t)card->action_count);
        if (card->actions == NULL) allocate_error("read_cards 1");
        for (size_t j = 0; j < (size_t)card->action_count; ++j) {
            char move;
            check = fscanf(file, "%hhd %hhd %hhd %zu", &card->actions[j].read, &card->actions[j].write, &move, &card->actions[j].next);
            if (check == EOF || check != 4) {
                fprintf(stderr, "Failed to read action for card %zu, action %zu\n", i, j);
                exit(EXIT_FAILURE);
            }
            if (move == 'R' || move == 'L' || move == 'S')
                card->actions[j].move = move;
            else if (move == 'r' || move == 'l' || move == 's')
                card->actions[j].move = (char)(move - ('a' - 'A'));
            else {
                fprintf(stderr, "Invalid move character: %c\n", move);
                exit(EXIT_FAILURE);
            }
        }
    }
    fclose(file);
}

static void free_cards(void) {
    for (size_t i = 0; i < card_count; ++i)
        free(cards[i].actions);
    free(cards);
}

char write_move_and_read(int is_left, char write_value) {
    head_tape->data[head_pos] = write_value;
    if (is_left) {
        if (head_pos > 0)
            --head_pos;
        else {
            if (head_tape->left_bound == NULL)
                create_tapes(1, head_tape);
            else {
                head_tape = head_tape->left_bound;
                head_pos = tape_size - 1;
            }
        }
    } else {
        if (head_pos < tape_size - 1)
            ++head_pos;
        else {
            if (head_tape->right_bound == NULL)
                create_tapes(0, head_tape);
            else {
                head_tape = head_tape->right_bound;
                head_pos = 0;
            }
        }
    }
    return head_tape->data[head_pos];
}

void allocate_error(char* function_name) {
    fprintf(stderr, "Failed to allocate memory in function: %s\n", function_name);
    exit(EXIT_FAILURE);
}
