/* Parses curl's markdown-lite option docs into blocks of styled spans. */
#ifndef CURLUI_DOCS_H
#define CURLUI_DOCS_H

typedef enum { SPAN_TEXT, SPAN_CODE, SPAN_BOLD, SPAN_LINK } span_kind;

typedef struct {
  span_kind kind;
  char *text;
  int opt;   /* SPAN_LINK: option index */
} doc_span;

typedef enum { BLOCK_PARA, BLOCK_TAG, BLOCK_CODE, BLOCK_BULLET } block_kind;

typedef struct {
  block_kind kind;
  doc_span *spans;
  int nspans;
} doc_block;

typedef struct {
  doc_block *blocks;
  int nblocks;
} doc_t;

/* `## value` is BLOCK_TAG, 4-space indent is BLOCK_CODE (one span per block,
   lines joined by \n), `- ` and `* ` are BLOCK_BULLET. */
int docs_parse(const char *markdown, doc_t *out);
void docs_free(doc_t *d);

#endif
