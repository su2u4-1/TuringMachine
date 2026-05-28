#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

typedef unsigned char Cell;

typedef struct {
    size_t next;
    Cell read;
    Cell write;
    char move;
} Action;

typedef struct {
    size_t state;
    size_t action_count;
    Action* actions;
} Card;

typedef struct TapeCell TapeCell;
struct TapeCell {
    TapeCell* left;
    TapeCell* right;
    Cell value;
    bool visible;
};

typedef struct {
    TapeCell* head;
    TapeCell* leftmost;
    TapeCell* rightmost;
    TapeCell* visible_leftmost;
    TapeCell* visible_rightmost;
    ptrdiff_t head_offset;
} Tape;

typedef struct {
    Card* cards;
    size_t card_count;
    Tape tape;
    FILE* trace_file;
} Machine;

typedef struct {
    char* card_file;
    char* tape_file;
    char* output_file;
    size_t max_steps;
    bool trace_enabled;
    bool tape_is_text;
} Options;

static void print_help(const char* program_name);
static void parse_arguments(int argc, char* argv[], Options* options);
static void machine_init(Machine* machine, const char* card_file, const char* tape_file, bool tape_is_text, const char* output_file);
static void machine_run(Machine* machine, bool trace_enabled, size_t max_steps);
static void machine_cleanup(Machine* machine);

static void read_cards(Machine* machine, const char* card_file);
static void read_initial_tape(Tape* tape, const char* tape_file, bool text_mode);
static void tape_init_blank(Tape* tape);
static void tape_free(Tape* tape);
static void tape_step(Tape* tape, char move, Cell write_value);

static void print_trace(FILE* output, size_t step, size_t card_index, const Action* action, Cell read_value, const Tape* tape);

static void* alloc(size_t count, size_t size, const char* function_name);
noreturn static void allocate_error(const char* function_name);
static size_t parse_size_argument(const char* value, const char* flag_name);
static void print_cell_padding(FILE* output, size_t cell_count);

static TapeCell* tape_cell_new(Cell value);
static void tape_reveal_cell(Tape* tape, TapeCell* cell);
static int read_text_cell(FILE* file, Cell* value);
static int read_binary_cell(FILE* file, Cell* value);
static bool is_tape_delimiter(int ch);
static bool has_suffix(const char* value, const char* suffix);

static void print_help(const char* program_name) {
    printf("Usage: %s <card_file> [(-t | --tape) <tape_file>] [(-o | --output) <output_file>] [(-s | --step) <max_steps>] [-p | --print] [-h | --help]\n", program_name);
    printf(
        "Flags:\n"
        "  -t <tape_file>\n"
        "  --tape   <tape_file>   Specify the initial tape file (text or binary)\n"
        "  -o <output_file>\n"
        "  --output <output_file> Mirror the step trace to a file\n"
        "  -s <max_steps>\n"
        "  --step   <max_steps>   Set a maximum number of steps to execute (default: unlimited)\n"
        "  -p\n"
        "  --print                Print the step trace after each step\n"
        "  -h\n"
        "  --help                 Show this help message\n");
}

static void parse_arguments(int argc, char* argv[], Options* options) {
    if (argc < 2) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        exit(EXIT_SUCCESS);
    }

    if (argv[1][0] == '-') {
        fprintf(stderr, "Expected card file as the first argument\n");
        exit(EXIT_FAILURE);
    }

    options->card_file = argv[1];
    options->tape_file = NULL;
    options->output_file = NULL;
    options->trace_enabled = false;
    options->tape_is_text = false;
    options->max_steps = SIZE_MAX;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tape") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                fprintf(stderr, "Expected tape file after -t/--tape flag\n");
                exit(EXIT_FAILURE);
            }
            options->tape_file = argv[++i];
            options->tape_is_text = has_suffix(options->tape_file, ".txt");
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                fprintf(stderr, "Expected output file after -o/--output flag\n");
                exit(EXIT_FAILURE);
            }
            options->output_file = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--print") == 0) {
            options->trace_enabled = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--step") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                fprintf(stderr, "Expected max steps after -s/--step flag\n");
                exit(EXIT_FAILURE);
            }
            options->max_steps = parse_size_argument(argv[++i], "-s/--step");
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            exit(EXIT_SUCCESS);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
}

static void* alloc(size_t count, size_t size, const char* function_name) {
    void* memory = calloc(count, size);
    if (memory == NULL) {
        allocate_error(function_name);
    }
    return memory;
}

static TapeCell* tape_cell_new(Cell value) {
    TapeCell* cell = (TapeCell*)alloc(1, sizeof(*cell), "tape cell");
    cell->value = value;
    return cell;
}

static size_t parse_size_argument(const char* value, const char* flag_name) {
    errno = 0;
    char* end = NULL;
    size_t parsed = strtoull(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed > SIZE_MAX) {
        fprintf(stderr, "Invalid value for %s: %s\n", flag_name, value);
        exit(EXIT_FAILURE);
    }

    return (size_t)parsed;
}

static void print_cell_padding(FILE* output, size_t cell_count) {
    for (size_t i = 0; i < cell_count; ++i) {
        fputs("    ", output);
    }
}

static void tape_reveal_cell(Tape* tape, TapeCell* cell) {
    if (cell == NULL || cell->visible) {
        return;
    }

    cell->visible = true;
    if (cell->left == NULL) {
        tape->visible_leftmost = cell;
        if (cell == tape->head) {
            tape->head_offset = 0;
        }
    }
    if (cell->right == NULL) {
        tape->visible_rightmost = cell;
    }
}

static void tape_init_blank(Tape* tape) {
    TapeCell* cell = tape_cell_new(0);
    cell->visible = true;
    tape->head = cell;
    tape->leftmost = cell;
    tape->rightmost = cell;
    tape->visible_leftmost = cell;
    tape->visible_rightmost = cell;
    tape->head_offset = 0;
}

static void tape_free(Tape* tape) {
    TapeCell* cell = tape->leftmost;
    if (cell == NULL) {
        cell = tape->head;
        while (cell != NULL && cell->left != NULL) {
            cell = cell->left;
        }
    }

    while (cell != NULL) {
        TapeCell* next = cell->right;
        free(cell);
        cell = next;
    }

    tape->head = NULL;
    tape->leftmost = NULL;
    tape->rightmost = NULL;
    tape->visible_leftmost = NULL;
    tape->visible_rightmost = NULL;
    tape->head_offset = 0;
}

static bool is_tape_delimiter(int ch) {
    return ch == ',' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

static int read_text_cell(FILE* file, Cell* value) {
    long parsed = 0;
    int check = fscanf(file, " %ld", &parsed);
    if (check == 1) {
        int ch = fgetc(file);
        while (ch != EOF && is_tape_delimiter(ch)) {
            ch = fgetc(file);
        }
        if (ch != EOF && ungetc(ch, file) == EOF) {
            return -1;
        }
        if (parsed < 0 || parsed > UCHAR_MAX) {
            return -1;
        }
        *value = (Cell)parsed;
        return 1;
    }
    if (check == EOF) {
        return 0;
    }
    return -1;
}

static int read_binary_cell(FILE* file, Cell* value) {
    size_t check = fread(value, sizeof(*value), 1, file);
    if (check == 1) {
        return 1;
    }
    if (feof(file)) {
        return 0;
    }
    return -1;
}

static void read_initial_tape(Tape* tape, const char* tape_file, bool text_mode) {
    FILE* file = fopen(tape_file, text_mode ? "r" : "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to open tape file: %s\n", tape_file);
        exit(EXIT_FAILURE);
    }

    TapeCell* first = NULL;
    TapeCell* last = NULL;
    size_t cell_count = 0;

    while (true) {
        Cell value = 0;
        int status = text_mode ? read_text_cell(file, &value) : read_binary_cell(file, &value);
        if (status == 0) {
            break;
        }
        if (status < 0) {
            fclose(file);
            fprintf(stderr, "Failed to read tape file: %s\n", tape_file);
            exit(EXIT_FAILURE);
        }

        TapeCell* cell = tape_cell_new(value);
        cell->visible = true;
        if (first == NULL) {
            first = cell;
        } else {
            last->right = cell;
            cell->left = last;
        }
        last = cell;
        ++cell_count;
    }

    fclose(file);
    tape_free(tape);

    if (cell_count == 0) {
        tape_init_blank(tape);
        return;
    }

    tape->head = first;
    tape->leftmost = first;
    tape->rightmost = last;
    tape->visible_leftmost = first;
    tape->visible_rightmost = last;
    tape->head_offset = 0;
}

static void tape_step(Tape* tape, char move, Cell write_value) {
    tape->head->value = write_value;
    tape_reveal_cell(tape, tape->head);

    if (move == 'L') {
        if (tape->head->left == NULL) {
            TapeCell* new_head = tape_cell_new(0);
            new_head->right = tape->head;
            tape->head->left = new_head;
            tape->leftmost = new_head;
            tape->head = new_head;
            --tape->head_offset;
        } else {
            tape->head = tape->head->left;
            --tape->head_offset;
        }
    } else if (move == 'R') {
        if (tape->head->right == NULL) {
            TapeCell* new_head = tape_cell_new(0);
            new_head->left = tape->head;
            tape->head->right = new_head;
            tape->rightmost = new_head;
            tape->head = new_head;
        } else {
            tape->head = tape->head->right;
        }
        ++tape->head_offset;
    } else if (move != 'S') {
        fprintf(stderr, "Invalid move character: %c\n", move);
        exit(EXIT_FAILURE);
    }
}

static void read_cards(Machine* machine, const char* card_file) {
    FILE* file = fopen(card_file, "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open card file: %s\n", card_file);
        exit(EXIT_FAILURE);
    }

    size_t card_count = 0;
    if (fscanf(file, " %zu", &card_count) != 1 || card_count == 0) {
        fprintf(stderr, "Failed to read card count from file: %s\n", card_file);
        exit(EXIT_FAILURE);
    }

    machine->cards = (Card*)alloc(card_count, sizeof(*machine->cards), "read_cards 0");
    machine->card_count = card_count;

    for (size_t card_index = 0; card_index < card_count; ++card_index) {
        Card* card = &machine->cards[card_index];
        size_t state = 0;
        size_t action_count = 0;
        if (fscanf(file, " %zu %zu", &state, &action_count) != 2 || state != card_index || action_count > UCHAR_MAX) {
            fprintf(stderr, "Failed to read card %zu from file: %s\n", card_index, card_file);
            exit(EXIT_FAILURE);
        }

        card->state = state;
        card->action_count = action_count;
        card->actions = action_count == 0 ? NULL : (Action*)alloc(action_count, sizeof(*card->actions), "read_cards 1");

        for (size_t action_index = 0; action_index < action_count; ++action_index) {
            long read_value = 0;
            long write_value = 0;
            char move = '\0';
            size_t next_state = 0;
            if (fscanf(file, " %ld %ld %c %zu", &read_value, &write_value, &move, &next_state) != 4 || read_value < 0 || read_value > UCHAR_MAX || write_value < 0 || write_value > UCHAR_MAX || next_state >= card_count) {
                fprintf(stderr, "Failed to read action for card %zu, action %zu\n", card_index, action_index);
                exit(EXIT_FAILURE);
            }

            move = (char)toupper((unsigned char)move);

            if (move != 'R' && move != 'L' && move != 'S') {
                fprintf(stderr, "Invalid move character: %c\n", move);
                exit(EXIT_FAILURE);
            }

            card->actions[action_index].read = (Cell)read_value;
            card->actions[action_index].write = (Cell)write_value;
            card->actions[action_index].move = move;
            card->actions[action_index].next = next_state;
        }
    }

    fclose(file);
}

static void print_trace(FILE* output, size_t step, size_t card_index, const Action* action, Cell read_value, const Tape* tape) {
    fprintf(output, "Step %zu: Card %zu, Read %u, Write %u, Move %c, Next Card %zu\n", step, card_index, (unsigned int)read_value, (unsigned int)action->write, action->move, action->next);

    size_t left_padding = tape->head_offset < 0 ? (size_t)(-tape->head_offset) : 0;
    size_t right_padding = tape->head_offset > 0 ? (size_t)tape->head_offset : 0;

    print_cell_padding(output, left_padding);

    for (const TapeCell* cell = tape->visible_leftmost; cell != NULL; cell = cell->right) {
        fprintf(output, "%3u ", (unsigned int)cell->value);
        if (cell == tape->visible_rightmost) {
            break;
        }
    }

    fprintf(output, "\n");

    print_cell_padding(output, left_padding);
    print_cell_padding(output, right_padding);

    fputs("  ^\n", output);
}

static void machine_init(Machine* machine, const char* card_file, const char* tape_file, bool tape_is_text, const char* output_file) {
    machine->cards = NULL;
    machine->card_count = 0;
    machine->trace_file = NULL;
    machine->tape.head = NULL;
    machine->tape.leftmost = NULL;
    machine->tape.rightmost = NULL;
    machine->tape.head_offset = 0;

    if (tape_file != NULL) {
        read_initial_tape(&machine->tape, tape_file, tape_is_text);
    } else {
        tape_init_blank(&machine->tape);
    }

    read_cards(machine, card_file);

    if (output_file != NULL) {
        machine->trace_file = fopen(output_file, "w");
        if (machine->trace_file == NULL) {
            fprintf(stderr, "Failed to open output file: %s\n", output_file);
            exit(EXIT_FAILURE);
        }
    }
}

static void machine_run(Machine* machine, bool trace_enabled, size_t max_steps) {
    size_t current_state = 0;

    for (size_t step = 0; step < max_steps; ++step) {
        if (current_state >= machine->card_count) {
            break;
        }

        Card* card = &machine->cards[current_state];
        Cell read_value = machine->tape.head->value;
        const Action* action = NULL;

        for (size_t action_index = 0; action_index < card->action_count; ++action_index) {
            if (card->actions[action_index].read == read_value) {
                action = &card->actions[action_index];
                break;
            }
        }

        if (action == NULL) {
            break;
        }

        size_t card_index = current_state;
        current_state = action->next;
        tape_step(&machine->tape, action->move, action->write);

        if (trace_enabled || machine->trace_file != NULL) {
            if (trace_enabled) {
                print_trace(stdout, step, card_index, action, read_value, &machine->tape);
            }
            if (machine->trace_file != NULL) {
                print_trace(machine->trace_file, step, card_index, action, read_value, &machine->tape);
            }
        }
    }
}

static void machine_cleanup(Machine* machine) {
    if (machine->trace_file != NULL) {
        fclose(machine->trace_file);
        machine->trace_file = NULL;
    }

    tape_free(&machine->tape);

    for (size_t i = 0; i < machine->card_count; ++i) {
        free(machine->cards[i].actions);
    }

    free(machine->cards);
    machine->cards = NULL;
    machine->card_count = 0;
}

static bool has_suffix(const char* value, const char* suffix) {
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length && strcmp(value + value_length - suffix_length, suffix) == 0;
}

int main(int argc, char* argv[]) {
    Options options = {0};
    parse_arguments(argc, argv, &options);

    Machine machine = {0};
    machine_init(&machine, options.card_file, options.tape_file, options.tape_is_text, options.output_file);
    machine_run(&machine, options.trace_enabled, options.max_steps);
    machine_cleanup(&machine);

    return EXIT_SUCCESS;
}

void allocate_error(const char* function_name) {
    fprintf(stderr, "Failed to allocate memory in function: %s\n", function_name);
    exit(EXIT_FAILURE);
}
