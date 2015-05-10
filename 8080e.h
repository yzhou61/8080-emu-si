struct cpu_mem_t {
    unsigned char *mem;
    void *state;
};

struct keyboard_t {
    unsigned char coin;
    unsigned char p1_start;
    unsigned char p1_shoot;
    unsigned char p1_left;
    unsigned char p1_right;
    unsigned char p2_start;
    unsigned char p2_shoot;
    unsigned char p2_left;
    unsigned char p2_right;
};

struct cpu_mem_t *init_machine(const char *bin_name, struct keyboard_t *keyboard);

void deinit_machine(struct cpu_mem_t *machine);

void generate_intr(struct cpu_mem_t *machine, int intr_num);

void execute(struct cpu_mem_t *machine, int cycles);
