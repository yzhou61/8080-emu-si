struct cpu_mem_t {
    unsigned char *mem;
    void *state;
};

struct keyboard_t {
    int esc;
};

struct cpu_mem_t *init_machine(const char *bin_name, struct keyboard_t *keyboard);

void deinit_machine(struct cpu_mem_t *machine);

void generate_intr(struct cpu_mem_t *machine, int intr_num);

void execute(struct cpu_mem_t *machine, int cycles);
