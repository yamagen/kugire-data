/*
 * kugire_from_kokin.c
 *
 * 目的:
 *   01kokin.txt の ¥X 行にある五句区切りを使い、
 *   形態素データ a/b/c (表層/品詞/かな) と突き合わせて
 *   各句末(第1-第4句末)に K1/K2/K0 を付ける。
 *
 * コンパイル:
 *   cc -O2 -std=c11 -Wall -Wextra -o kugire_from_kokin kugire_from_kokin.c
 *
 * 使い方:
 *   ./kugire_from_kokin 01kokin.txt morph.txt > out.jsonl
 *
 * 形態素ファイルの想定:
 *   ¥Ｎ00018
 *   かすかの/名-地名/かすがの ゝ/格助/の とふひ/名/とぶひ ...
 *
 * 出力:
 *   1首ごとに1行の JSON
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 65536
#define MAX_POEMS 10000
#define MAX_TOKENS 1024
#define MAX_PHRASES 5

static char *normalize_morph_id_copy(const char *raw);

typedef struct {
  char *surface;
  char *pos;
  char *kana;
} Token;

typedef struct {
  char *id;
  char *w_line;
  char *x_line;
} Poem;

typedef struct {
  char *id;
  char *morph_line;
} MorphEntry;

typedef struct {
  int phrase_no;
  int start_token;
  int end_token;
  const char *k_class;
} Boundary;

static void die(const char *msg) {
  fprintf(stderr, "ERROR: %s\n", msg);
  exit(1);
}

static char *xstrdup(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s);
  char *p = (char *)malloc(n + 1);
  if (!p)
    die("malloc failed");
  memcpy(p, s, n + 1);
  return p;
}

static void rstrip_newline(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
    s[n - 1] = '\0';
    n--;
  }
}

static void ltrim_inplace(char *s) {
  size_t i = 0, j = 0;
  while (s[i] && (unsigned char)s[i] <= ' ')
    i++;
  if (i > 0) {
    while (s[i])
      s[j++] = s[i++];
    s[j] = '\0';
  }
}

static void rtrim_inplace(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (unsigned char)s[n - 1] <= ' ') {
    s[n - 1] = '\0';
    n--;
  }
}

static void trim_inplace(char *s) {
  ltrim_inplace(s);
  rtrim_inplace(s);
}

static int starts_with(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* 全角数字を半角へ */
static void zenkaku_digits_to_ascii(char *s) {
  static const char *zen[] = {"０", "１", "２", "３", "４",
                              "５", "６", "７", "８", "９"};
  static const char asc[] = "0123456789";

  char buf[MAX_LINE];
  size_t bi = 0;
  size_t i = 0;

  while (s[i]) {
    int matched = 0;
    for (int d = 0; d < 10; d++) {
      size_t zlen = strlen(zen[d]);
      if (strncmp(&s[i], zen[d], zlen) == 0) {
        buf[bi++] = asc[d];
        i += zlen;
        matched = 1;
        break;
      }
    }
    if (!matched) {
      buf[bi++] = s[i++];
    }
    if (bi + 8 >= sizeof(buf))
      die("buffer overflow in zenkaku_digits_to_ascii");
  }
  buf[bi] = '\0';
  strcpy(s, buf);
}

static char *normalize_id_copy(const char *raw) {
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s", raw);
  trim_inplace(tmp);
  zenkaku_digits_to_ascii(tmp);

  /* 数字だけ拾う */
  char digits[64];
  size_t di = 0;
  for (size_t i = 0; tmp[i]; i++) {
    if (isdigit((unsigned char)tmp[i]))
      digits[di++] = tmp[i];
  }
  digits[di] = '\0';

  if (di == 0)
    return NULL;

  char out[64];
  if (di >= 5) {
    snprintf(out, sizeof(out), "%s", digits);
  } else {
    snprintf(out, sizeof(out), "%05d", atoi(digits));
  }
  return xstrdup(out);
}

/* UTF-8 の特定文字列を削除 */
static char *remove_substrings_copy(const char *src) {
  const char *remove_list[] = {"／", " ", "　", "\t", "ゝ", "ゞ", NULL};
  size_t n = strlen(src);
  char *out = (char *)malloc(n + 1);
  if (!out)
    die("malloc failed");
  size_t oi = 0;

  for (size_t i = 0; src[i];) {
    int removed = 0;
    for (int r = 0; remove_list[r]; r++) {
      size_t m = strlen(remove_list[r]);
      if (strncmp(src + i, remove_list[r], m) == 0) {
        i += m;
        removed = 1;
        break;
      }
    }
    if (!removed) {
      out[oi++] = src[i++];
    }
  }
  out[oi] = '\0';
  return out;
}

static int split_phrases(const char *x_line, char *phrases[MAX_PHRASES]) {
  char *copy = xstrdup(x_line);
  int count = 0;

  char *p = copy;
  while (*p && count < MAX_PHRASES) {
    char *sep = strstr(p, "／");
    if (sep) {
      *sep = '\0';
      phrases[count++] = remove_substrings_copy(p);
      p = sep + strlen("／");
    } else {
      phrases[count++] = remove_substrings_copy(p);
      break;
    }
  }
  free(copy);
  return count;
}

static int parse_tokens(const char *line, Token tokens[MAX_TOKENS]) {
  char *copy = xstrdup(line);
  int count = 0;

  char *saveptr1 = NULL;
  char *item = strtok_r(copy, " \t", &saveptr1);
  while (item) {
    if (count >= MAX_TOKENS)
      die("too many tokens");

    char *p1 = strchr(item, '/');
    if (!p1) {
      item = strtok_r(NULL, " \t", &saveptr1);
      continue;
    }
    char *p2 = strchr(p1 + 1, '/');
    if (!p2) {
      item = strtok_r(NULL, " \t", &saveptr1);
      continue;
    }

    *p1 = '\0';
    *p2 = '\0';

    tokens[count].surface = xstrdup(item);
    tokens[count].pos = xstrdup(p1 + 1);
    tokens[count].kana = xstrdup(p2 + 1);
    count++;

    item = strtok_r(NULL, " \t", &saveptr1);
  }

  free(copy);
  return count;
}

static const char *classify_token(const char *pos) {
  /* 強候補 */
  if (strstr(pos, "命"))
    return "K1";
  if (strstr(pos, "終助"))
    return "K1";
  if (strstr(pos, "係助"))
    return "K1";

  /*
   * 活用表示で終止系を拾う
   * 例: 過-終:けり:けり
   *     推-終体:む:む
   */
  if (strstr(pos, "-終:"))
    return "K1";
  if (strstr(pos, "-終体:"))
    return "K1";
  if (strstr(pos, "-已:"))
    return "K1"; /* こそ已然を広めに拾う */

  /* 体言止め候補 */
  if (starts_with(pos, "名"))
    return "K2";

  /* 非候補 */
  if (strstr(pos, "格助"))
    return "K0";
  if (strstr(pos, "接助"))
    return "K0";

  /*
   * 用形や体言連体は継続しやすいので K0 寄り
   * ただし 終体 は上で先に拾っている
   */
  if (strstr(pos, "-用:"))
    return "K0";
  if (strstr(pos, "-体:"))
    return "K0";

  /* その他は保留的に K2 */
  return "K2";
}

static int align_phrases_with_tokens(char *phrases[MAX_PHRASES],
                                     int phrase_count, Token tokens[MAX_TOKENS],
                                     int token_count,
                                     Boundary bounds[MAX_PHRASES]) {
  int idx = 0;

  for (int p = 0; p < phrase_count; p++) {
    char buf[MAX_LINE];
    buf[0] = '\0';
    int start = idx;

    while (idx < token_count) {
      char *nk = remove_substrings_copy(tokens[idx].kana);
      if (strlen(buf) + strlen(nk) + 1 >= sizeof(buf)) {
        free(nk);
        die("alignment buffer overflow");
      }
      strcat(buf, nk);
      free(nk);
      idx++;

      if (strcmp(buf, phrases[p]) == 0) {
        bounds[p].phrase_no = p + 1;
        bounds[p].start_token = start;
        bounds[p].end_token = idx - 1;
        bounds[p].k_class = classify_token(tokens[idx - 1].pos);
        break;
      }

      /* 句文字列を追い越したら失敗 */
      if (strlen(buf) > strlen(phrases[p])) {
        return -1;
      }
    }

    if (idx > token_count)
      return -1;
    if (bounds[p].end_token < bounds[p].start_token)
      return -1;
  }

  return 0;
}

static void json_escape_print(FILE *out, const char *s) {
  fputc('"', out);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '\\')
      fputs("\\\\", out);
    else if (*p == '"')
      fputs("\\\"", out);
    else if (*p == '\n')
      fputs("\\n", out);
    else if (*p == '\r')
      fputs("\\r", out);
    else if (*p == '\t')
      fputs("\\t", out);
    else
      fputc(*p, out);
  }
  fputc('"', out);
}

static int load_kokin(const char *path, Poem poems[MAX_POEMS]) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    die("cannot open kokin file");

  char line[MAX_LINE];
  int count = 0;
  int current = -1;

  while (fgets(line, sizeof(line), fp)) {
    rstrip_newline(line);

    if (starts_with(line, "¥Ｎ")) {
      char *id = normalize_id_copy(line + strlen("¥Ｎ"));
      if (!id)
        continue;
      if (count >= MAX_POEMS)
        die("too many poems in kokin file");

      poems[count].id = id;
      poems[count].w_line = NULL;
      poems[count].x_line = NULL;
      current = count;
      count++;
    } else if (current >= 0 && starts_with(line, "¥Ｗ")) {
      poems[current].w_line = xstrdup(line + strlen("¥Ｗ"));
    } else if (current >= 0 && starts_with(line, "¥Ｘ")) {
      poems[current].x_line = xstrdup(line + strlen("¥Ｘ"));
    }
  }

  fclose(fp);
  return count;
}

static int load_morph(const char *path, MorphEntry morphs[MAX_POEMS]) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    die("cannot open morph file");

  char line[MAX_LINE];
  int count = 0;
  char *current_id = NULL;

  while (fgets(line, sizeof(line), fp)) {
    rstrip_newline(line);
    trim_inplace(line);
    if (line[0] == '\0')
      continue;

    if (starts_with(line, "¥Ｎ")) {
      free(current_id);
      current_id = normalize_id_copy(line + strlen("¥Ｎ"));
      continue;
    }

    /* 5桁数字 + 空白 + 形態素列 も許す */
    {
      char temp[MAX_LINE];
      snprintf(temp, sizeof(temp), "%s", line);
      char *sp = strpbrk(temp, " \t");
      if (sp) {
        *sp = '\0';
        char *id_try = normalize_morph_id_copy(temp);
        if (id_try && strlen(id_try) == 5) {
          morphs[count].id = id_try;
          trim_inplace(sp + 1);
          morphs[count].morph_line = xstrdup(sp + 1);
          count++;
          continue;
        }
        if (id_try)
          free(id_try);
      }
    }

    if (current_id) {
      if (count >= MAX_POEMS)
        die("too many morph entries");
      morphs[count].id = xstrdup(current_id);
      morphs[count].morph_line = xstrdup(line);
      count++;
      free(current_id);
      current_id = NULL;
    }
  }

  free(current_id);
  fclose(fp);
  return count;
}

static MorphEntry *find_morph(MorphEntry morphs[MAX_POEMS], int morph_count,
                              const char *id) {
  for (int i = 0; i < morph_count; i++) {
    if (strcmp(morphs[i].id, id) == 0)
      return &morphs[i];
  }
  return NULL;
}

static void free_tokens(Token tokens[MAX_TOKENS], int n) {
  for (int i = 0; i < n; i++) {
    free(tokens[i].surface);
    free(tokens[i].pos);
    free(tokens[i].kana);
  }
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s 01kokin.txt morph.txt\n", argv[0]);
    return 1;
  }

  Poem poems[MAX_POEMS];
  MorphEntry morphs[MAX_POEMS];

  int poem_count = load_kokin(argv[1], poems);
  int morph_count = load_morph(argv[2], morphs);

  for (int i = 0; i < poem_count; i++) {
    if (!poems[i].id || !poems[i].x_line)
      continue;

    MorphEntry *m = find_morph(morphs, morph_count, poems[i].id);
    if (!m)
      continue;

    Token tokens[MAX_TOKENS];
    int token_count = parse_tokens(m->morph_line, tokens);

    char *phrases[MAX_PHRASES] = {0};
    int phrase_count = split_phrases(poems[i].x_line, phrases);

    if (phrase_count != 5) {
      fprintf(stderr, "skip %s: phrase_count=%d (not 5)\n", poems[i].id,
              phrase_count);
      free_tokens(tokens, token_count);
      for (int k = 0; k < phrase_count; k++)
        free(phrases[k]);
      continue;
    }

    Boundary bounds[MAX_PHRASES];
    for (int b = 0; b < MAX_PHRASES; b++) {
      bounds[b].phrase_no = 0;
      bounds[b].start_token = 0;
      bounds[b].end_token = -1;
      bounds[b].k_class = NULL;
    }

    if (align_phrases_with_tokens(phrases, phrase_count, tokens, token_count,
                                  bounds) != 0) {
      fprintf(stderr, "align failed: %s\n", poems[i].id);
      free_tokens(tokens, token_count);
      for (int k = 0; k < phrase_count; k++)
        free(phrases[k]);
      continue;
    }

    /* JSON 1行出力 */
    printf("{");
    printf("\"id\":");
    json_escape_print(stdout, poems[i].id);
    printf(",");

    if (poems[i].w_line) {
      printf("\"w\":");
      json_escape_print(stdout, poems[i].w_line);
      printf(",");
    }

    printf("\"x\":");
    json_escape_print(stdout, poems[i].x_line);
    printf(",");

    printf("\"boundaries\":[");
    for (int p = 0; p < 4; p++) { /* 第1-第4句末のみ */
      int end = bounds[p].end_token;
      if (p > 0)
        printf(",");
      printf("{");
      printf("\"phrase\":%d,", p + 1);
      printf("\"end_token_surface\":");
      json_escape_print(stdout, tokens[end].surface);
      printf(",");
      printf("\"end_token_pos\":");
      json_escape_print(stdout, tokens[end].pos);
      printf(",");
      printf("\"end_token_kana\":");
      json_escape_print(stdout, tokens[end].kana);
      printf(",");
      printf("\"k_class\":");
      json_escape_print(stdout, bounds[p].k_class);
      printf("}");
    }
    printf("]");

    printf("}\n");

    free_tokens(tokens, token_count);
    for (int k = 0; k < phrase_count; k++)
      free(phrases[k]);
  }

  for (int i = 0; i < poem_count; i++) {
    free(poems[i].id);
    free(poems[i].w_line);
    free(poems[i].x_line);
  }
  for (int i = 0; i < morph_count; i++) {
    free(morphs[i].id);
    free(morphs[i].morph_line);
  }

  return 0;
}

static char *normalize_morph_id_copy(const char *raw) {
  char *id = normalize_id_copy(raw);
  if (!id)
    return NULL;

  /* 10001 → 00001 */
  if (strlen(id) == 5 && id[0] == '1') {
    int n = atoi(id + 1);
    char buf[16];
    snprintf(buf, sizeof(buf), "%05d", n);
    free(id);
    return xstrdup(buf);
  }

  return id;
}
