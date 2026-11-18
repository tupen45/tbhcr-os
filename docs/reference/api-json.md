# JSON Reference (`json.h`)

MyOS bundles **JSMN** ("Jasmine"), a minimal recursive-descent JSON parser,
as `json.c`/`json.h`. It tokenizes a JSON document into an array of
`jsmntok_t` without allocating — the caller provides the token pool.

## Types (`json.h`)

```c
typedef enum {
    JSMN_UNDEFINED = 0, JSMN_OBJECT, JSMN_ARRAY,
    JSMN_STRING, JSMN_PRIMITIVE
} jsmntype_t;

typedef struct {
    jsmntype_t type;
    int start, end, size;     // byte offsets into the source string
#ifdef JSMN_PARENT_LINKS
    int parent;
#endif
} jsmntok_t;

typedef struct {
    unsigned int pos, toknext, toksuper;
    int num_tokens;            // capacity of token pool
    jsmntok_t *tokens;
} jsmn_parser;
```

## API

| Function | Description |
|----------|-------------|
| `void jsmn_init(jsmn_parser *p)` | Reset a parser (sets `tokens`/`num_tokens`). |
| `int jsmn_parse(jsmn_parser *p, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens)` | Parse `js`; returns token count, or a negative `jsmnerr` (`-1` NOMEM, `-2` INVAL, `-3` PART). |

> **Ownership:** `jsmn_parser.tokens` is **not** set by `jsmn_init`; you
> must point it at your own `jsmntok_t t[N]` and set `num_tokens = N`
> before calling `jsmn_parse`. Example usage in `json_viewer_ui()`:

```c
jsmn_parser p;
jsmntok_t t[128];
jsmn_init(&p);
p.tokens = t; p.num_tokens = 128;
int r = jsmn_parse(&p, body, strlen(body), t, 128);
```

## How the viewer uses it (`kernel.c`)

It iterates `t[1..r-1]`. When `t[i].type == JSMN_STRING` and the
previous token was also a string (i.e. a `key: value` pair inside an
object), it copies the key and value substrings (`start..end`) into a
`"key: value"` line and draws it. This is a heuristic for flat objects
like the `jsonplaceholder` `/users/1` response.

## Limitations

- No nested pretty-printing; the viewer only shows top-level
  string→value pairs.
- `size` on arrays/objects is the child count, not byte length.
- No number/boolean token *values* are extracted by the viewer (only
  string keys/values are displayed).

See also: [App Catalog — JSON Viewer](../apps/app-catalog.md).
