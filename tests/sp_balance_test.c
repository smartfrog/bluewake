#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sp_balance.h"

#include <assert.h>
#include <string.h>

static void matching_stack_pointer_is_not_a_mismatch(void) {
    assert(!ww_sp_balance_mismatch(0x80170C80u, 0x80170C80u));
}

static void restored_stack_pointer_difference_is_a_mismatch(void) {
    assert(ww_sp_balance_mismatch(0x80170C80u, 0x80170C78u));
}

static void record_line_names_block_and_both_stack_pointers(void) {
    char line[WW_SP_BALANCE_LINE_BYTES];
    const WwSpBalanceRecord record = {0x80170C40u, 0x80170C80u, 0x80170C78u};
    assert(ww_sp_balance_format_record(line, sizeof(line), &record) > 0);
    assert(strcmp(line, "[sp-balance] pc=80170C40 sp_in=80170C80 sp_out=80170C78") == 0);
}

static void record_line_rejects_a_short_buffer(void) {
    char line[8];
    const WwSpBalanceRecord record = {0x80170C40u, 0x80170C80u, 0x80170C78u};
    assert(ww_sp_balance_format_record(line, sizeof(line), &record) == -1);
}

static void window_line_names_span_and_words(void) {
    char line[WW_SP_BALANCE_LINE_BYTES];
    const uint32_t words[2] = {0xCCCCCCCCu, 0x00000001u};
    assert(ww_sp_balance_format_window(line, sizeof(line), 0x80170C80u, 0x80170C40u,
                                       words, 2u) > 0);
    assert(strcmp(line, "[sp-balance] window sp=80170C80 base=80170C40 words= CCCCCCCC 00000001") == 0);
}

static void window_line_holds_sixteen_words(void) {
    char line[WW_SP_BALANCE_LINE_BYTES];
    uint32_t words[WW_SP_BALANCE_WINDOW_WORDS];
    for (uint32_t index = 0u; index < WW_SP_BALANCE_WINDOW_WORDS; ++index)
        words[index] = 0xCCCCCCCCu;
    assert(ww_sp_balance_format_window(line, sizeof(line), 0x8040CF78u, 0x8040CF38u,
                                       words, WW_SP_BALANCE_WINDOW_WORDS) > 0);
    assert(strstr(line, "words= CCCCCCCC CCCCCCCC") != NULL);
}

int main(void) {
    matching_stack_pointer_is_not_a_mismatch();
    restored_stack_pointer_difference_is_a_mismatch();
    record_line_names_block_and_both_stack_pointers();
    record_line_rejects_a_short_buffer();
    window_line_names_span_and_words();
    window_line_holds_sixteen_words();
    return 0;
}
