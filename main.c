#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

typedef struct {
    size_t next;
    char read;
    char write;
    char move;
} Action;

typedef struct {
    Action* actions;
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
static Tape* head_tape = NULL;
static int head_pos = 0;
static Tape* start_tape = NULL;
static Tape* end_tape = NULL;
static int start_pos = -1;
static int end_pos = -1;

static void init(char* card_file, char* tape_file, int tape_mode, char* output_file);
static void run(char* output_file, int print_tape, size_t max_steps);

static void create_tapes(int is_left, Tape* now_tape);
static void read_initial_tape(char* tape_file, int mode);
static void free_tapes(void);
static void read_cards(char* card_file);
static void free_cards(void);

static char write_move_and_read(char move, char write_value);
noreturn static void allocate_error(char* function_name);

static void print_help(char* program_name) {
    printf("Usage: %s <card_file> [(-t | --tape) <tape_file>] [(-o | --output) <output_file>] [(-s | --step) <max_steps>] [-p | -print] [-h | --help]\n", program_name);
    printf(
        "Flags:\n"
        "  -t <tape_file>\n"
        "  --tape   <tape_file>   Specify the initial tape file (text or binary)\n"
        "  -o <output_file>\n"
        "  --output <output_file> Specify the output file for the final tape state\n"
        "  -s <max_steps>\n"
        "  --step   <max_steps>   Set a maximum number of steps to execute (default: 2147483647)\n"
        "  -p\n"
        "  --print                Print the final tape state to standard output\n"
        "  -h\n"
        "  --help                 Show this help message\n");
}

static void print_all_tape(char* output_file, size_t step, Card* card, char current_value, Action* action) {
    FILE* output = stdout;
    if (output_file != NULL) {
        output = fopen(output_file, "a");
        if (output == NULL) {
            fprintf(stderr, "Failed to open output file: %s\n", output_file);
            exit(EXIT_FAILURE);
        }
    }
    fprintf(output, "Step %zu: Card %zu, Read %d, Write %d, Move %c, Next Card %zu\n", step, card - cards, (unsigned char)current_value, (unsigned char)action->write, action->move, action->next);
    Tape* current_tape = start_tape;
    size_t head_index = 0;
    size_t found_head = 0;
    while (current_tape != NULL) {
        int start_pos_in_scope = (current_tape == start_tape) ? start_pos : 0;
        int end_pos_in_scope = (current_tape == end_tape) ? end_pos : tape_size;
        for (int i = start_pos_in_scope; i < end_pos_in_scope; ++i) {
            if (current_tape == head_tape && i == head_pos)
                found_head = head_index;
            else
                ++head_index;
            fprintf(output, "%3d ", (unsigned char)current_tape->data[i]);
        }
        current_tape = current_tape->right_bound;
    }
    fprintf(output, "\n");
    for (size_t i = 0; i < found_head; ++i)
        fprintf(output, "    ");
    fprintf(output, "  ^\n");
    if (output != NULL && output != stdout)
        fclose(output);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
    char* card_file = NULL;
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    } else if (argv[1][0] == '-') {
        fprintf(stderr, "Expected card file as the first argument\n");
        return EXIT_FAILURE;
    }
    card_file = argv[1];
    char* tape_file = NULL;
    char* output_file = NULL;
    int print_tape = 0;
    int tape_mode = 0;  // 1 for text, 0 for binary
    size_t max_steps = 2147483647;
    for (int i = 2; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 't' || (argv[i][1] == '-' && strcmp(argv[i], "--tape") == 0)) {
                if (i + 1 >= argc || argv[i + 1][0] == '-') {
                    fprintf(stderr, "Expected tape file after -t flag\n");
                    return EXIT_FAILURE;
                }
                tape_file = argv[++i];
                size_t len = strlen(tape_file);
                if (len >= 4 && strcmp(tape_file + len - 4, ".txt") == 0)
                    tape_mode = 1;
            } else if (argv[i][1] == 'o' || (argv[i][1] == '-' && strcmp(argv[i], "--output") == 0)) {
                if (i + 1 >= argc || argv[i + 1][0] == '-') {
                    fprintf(stderr, "Expected output file after -o flag\n");
                    return EXIT_FAILURE;
                }
                output_file = argv[++i];
            } else if (argv[i][1] == 'p' || (argv[i][1] == '-' && strcmp(argv[i], "--print") == 0)) {
                print_tape = 1;
            } else if (argv[i][1] == 's' || (argv[i][1] == '-' && strcmp(argv[i], "--step") == 0)) {
                if (i + 1 >= argc || argv[i + 1][0] == '-') {
                    fprintf(stderr, "Expected max steps after -s flag\n");
                    return EXIT_FAILURE;
                }
                max_steps = strtoull(argv[++i], NULL, 10);
            } else if (argv[i][1] == 'h' || (argv[i][1] == '-' && strcmp(argv[i], "--help") == 0)) {
                print_help(argv[0]);
                return EXIT_SUCCESS;
            } else {
                fprintf(stderr, "Unknown flag: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    init(card_file, tape_file, tape_mode, output_file);
    run(output_file, print_tape, max_steps);
    free_tapes();
    free_cards();
    return EXIT_SUCCESS;
}

void init(char* card_file, char* tape_file, int tape_mode, char* output_file) {
    ++tape_count;
    head_tape = (Tape*)malloc(sizeof(Tape));
    if (head_tape == NULL) allocate_error("init 0");
    // use calloc to initialize all cells to 0
    head_tape->data = (char*)calloc(tape_size, sizeof(char));
    if (head_tape->data == NULL) allocate_error("init 1");
    head_tape->left_bound = NULL;
    head_tape->right_bound = NULL;
    start_tape = head_tape;
    end_tape = head_tape;
    start_pos = 0;
    end_pos = 0;
    if (tape_file != NULL)
        read_initial_tape(tape_file, tape_mode);
    read_cards(card_file);
    // reset output file to empty
    FILE* output = fopen(output_file, "w");
    if (output == NULL) {
        fprintf(stderr, "Failed to open output file: %s\n", output_file);
        exit(EXIT_FAILURE);
    }
    fclose(output);
}

void run(char* output_file, int print_tape, size_t max_steps) {
    size_t current_card = 0;
    for (size_t step = 0; step < max_steps; ++step) {
        Card* card = &cards[current_card];
        char current_value = head_tape->data[head_pos];
        int action_index = -1;
        for (int i = 0; i < card->action_count; ++i) {
            if (card->actions[i].read == current_value) {
                action_index = i;
                break;
            }
        }
        if (action_index == -1) break;  // no matching action, halt
        Action* action = &card->actions[action_index];
        current_card = action->next;
        current_value = write_move_and_read(action->move, action->write);
        if (print_tape) {
            print_all_tape(NULL, step, card, current_value, action);
            print_all_tape(output_file, step, card, current_value, action);
        }
    }
}

void create_tapes(int is_left, Tape* now_tape) {
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
    int index = 0;
    while (index < tape_size) {
        char value;
        if (mode) {
            int check = fscanf(file, " %hhd%*[, \t\r\n\f\v]", &value);
            if (check != 1) break;
        } else {
            size_t check = fread(&value, sizeof(char), 1, file);
            if (check != 1) break;
        }
        head_tape->data[index++] = value;
    }
    fclose(file);
    start_pos = 0;
    end_pos = index;
}

void free_tapes(void) {
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

void read_cards(char* card_file) {
    FILE* file = fopen(card_file, "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open card file: %s\n", card_file);
        exit(EXIT_FAILURE);
    }
    int check = fscanf(file, " %zu", &card_count);
    if (check == EOF || check != 1) {
        fprintf(stderr, "Failed to read card count from file: %s\n", card_file);
        exit(EXIT_FAILURE);
    }
    cards = malloc(sizeof(Card) * card_count);
    if (cards == NULL) allocate_error("read_cards 0");
    for (size_t i = 0; i < card_count; ++i) {
        Card* card = &cards[i];
        check = fscanf(file, " %zu %hhd", &card->now_state, &card->action_count);
        if (check == EOF || check != 2) {
            fprintf(stderr, "Failed to read card %zu from file: %s\n", i, card_file);
            exit(EXIT_FAILURE);
        }
        card->actions = malloc(sizeof(Action*) * (size_t)card->action_count);
        if (card->actions == NULL) allocate_error("read_cards 1");
        for (size_t j = 0; j < (size_t)card->action_count; ++j) {
            char move;
            // check = 0;
            // check += fscanf(file, " %hhd", &card->actions[j].read);
            // check += fscanf(file, " %hhd", &card->actions[j].write);
            // check += fscanf(file, " %c", &move);
            // check += fscanf(file, " %zu", &card->actions[j].next);
            check = fscanf(file, " %hhd %hhd %c %zu", &card->actions[j].read, &card->actions[j].write, &move, &card->actions[j].next);
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

void free_cards(void) {
    for (size_t i = 0; i < card_count; ++i)
        free(cards[i].actions);
    free(cards);
}

char write_move_and_read(char move, char write_value) {
    head_tape->data[head_pos] = write_value;
    if (move == 'L') {
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
        if (head_tape == start_tape && head_pos < start_pos)
            start_pos = head_pos;
        else if (head_tape->right_bound == start_tape) {
            start_tape = head_tape;
            start_pos = tape_size - 1;
        }
    } else if (move == 'R') {
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
        if (head_tape == end_tape && head_pos > end_pos)
            end_pos = head_pos;
        else if (head_tape->left_bound == end_tape) {
            end_tape = head_tape;
            end_pos = 0;
        }
    } else if (move != 'S') {
        fprintf(stderr, "Invalid move character: %c\n", move);
        exit(EXIT_FAILURE);
    }
    return head_tape->data[head_pos];
}

void allocate_error(char* function_name) {
    fprintf(stderr, "Failed to allocate memory in function: %s\n", function_name);
    exit(EXIT_FAILURE);
}
