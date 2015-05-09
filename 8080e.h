struct machine_t {
    unsigned char *mem;
    void *state;
};

struct machine_t *init_machine(const char *bin_name);

void deinit_machine(struct machine_t *machine);

void generate_intr(struct machine_t *machine, int intr_num);

void execute(struct machine_t *machine, int cycles);
