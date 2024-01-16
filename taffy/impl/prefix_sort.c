#include "taf.h"
#include "tai.h"
#include "sonLib.h"

/*
 * Functions for sorting alignment rows by prefixes of their sequence name
 */

void sequence_prefix_destruct(Sequence_Prefix *sequence_prefix) {
    free(sequence_prefix->prefix);
    free(sequence_prefix);
}

Sequence_Prefix *sequence_prefix_construct(char *prefix, int64_t index) {
    Sequence_Prefix *sequence_prefix = st_calloc(1, sizeof(Sequence_Prefix));
    sequence_prefix->prefix = prefix;
    sequence_prefix->prefix_length = strlen(prefix);
    if(sequence_prefix->prefix_length == 0) {
        st_errAbort("Found an empty sequence prefix");
    }
    sequence_prefix->index = index;
    return sequence_prefix;
}

int sequence_prefix_cmp_fn(Sequence_Prefix *p1, Sequence_Prefix *p2) {
    return strcmp(p1->prefix, p2->prefix);
}

stList *sequence_prefix_load(FILE *sort_fh) {
    stList *prefixes_to_sort_by = stList_construct3(0, (void (*)(void *))sequence_prefix_destruct);
    int64_t index = 0;
    char *line;
    while((line = stFile_getLineFromFile(sort_fh)) != NULL) {
        stList *tokens = stString_split(line);
        if(stList_length(tokens) != 1) {
            st_errAbort("Expected exactly one string in sort file on line: %s", line);
        }
        Sequence_Prefix *sequence_prefix = sequence_prefix_construct(stList_pop(tokens), index++);
        stList_append(prefixes_to_sort_by, sequence_prefix);
        // Clean up
        free(line);
        stList_destruct(tokens);
    }
    stList_sort(prefixes_to_sort_by, (int (*)(const void *, const void *))sequence_prefix_cmp_fn);
    return prefixes_to_sort_by;
}

static int get_closest_prefix_cmp_fn(char *sequence_name, Sequence_Prefix *sp) {
    int64_t i = strcmp(sequence_name, sp->prefix);
    if(i > 0) { // If sequence_name is lexicographically larger than sequence_prefix could
        // be a prefix (can not be a prefix is i < 0)
        for(int64_t j=0; j<sp->prefix_length; j++) {
            if(sequence_name[j] != sp->prefix[j]) {
                return 1;
            }
        }
        return 0;
    }
    return i;
}

int64_t alignment_row_get_closest_sequence_prefix(Alignment_Row *row, stList *prefixes_to_sort_by) {
    // Binary search the sequence name
    Sequence_Prefix *sp = stList_binarySearch(prefixes_to_sort_by, row->sequence_name,
                                              (int (*)(const void *a, const void *b))get_closest_prefix_cmp_fn);
    if(sp == NULL) {
        st_logDebug("Did not find a valid prefix to match: %s\n", row->sequence_name);
    }
    return sp != NULL ? sp->index : -1; // Sequences that don't have a match will appear first in the sort
}

int alignment_sequence_prefix_cmp_fn(Alignment_Row *a1, Alignment_Row *a2,
                                     stList *prefixes_to_sort_by) {
    int i = alignment_row_get_closest_sequence_prefix(a1, prefixes_to_sort_by);
    int j = alignment_row_get_closest_sequence_prefix(a2, prefixes_to_sort_by);
    return i < j ? -1 : (i > j ? 1 : strcmp(a1->sequence_name, a2->sequence_name));
}

void alignment_sort_the_rows(Alignment *p_alignment, Alignment *alignment, stList *prefixes_to_sort_by) {
    // Get the rows
    stList *rows = alignment_get_rows_in_a_list(alignment->row);
    assert(stList_length(rows) == alignment->row_number); // Quick sanity check

    // Sort the rows by the prefix ordering
    stList_sort2(rows, (int (*)(const void *, const void *, void *))alignment_sequence_prefix_cmp_fn, prefixes_to_sort_by);

    // Re-connect the rows
    alignment_set_rows(alignment, rows);

    // Reset the alignment of the rows with the prior row
    if(p_alignment != NULL) {
        alignment_link_adjacent(p_alignment, alignment, 1);
    }
}
