#include <SDL.h>
#include <SDL_image.h>



float my_random(void)
{
  return ((float)rand()) /(float)RAND_MAX;
}

struct weighted_element {
  int id;       /* value returned by select_by_weight (on match) */
  float weight; /* weight of element */
  float start;  /* intern, will be modified */
  float end;    /* intern, will be modified */
};

int select_by_weight(int cnt, struct weighted_element *elements)
{
  float last = 0;
  int ret = -1;
  if (cnt > 0) {
    for (int i = 0; i < cnt; ++i) {
      elements[i].start = last;
      last = elements[i].end = elements[i].weight + last;
    }
    float rnd = my_random() * last;
    for (int i = 0; i < cnt; ++i) {
      if (rnd >= elements[i].start && rnd < elements[i].end) {
	ret = elements[i].id;
      }
    }
    if (ret == -1) {
      ret = elements[cnt - 1].id;
    }
  }
  return ret;
}

typedef struct bitfield32_st {
    uint32_t data;
} bitfield32;

int bitfield32_cmp(bitfield32 a, bitfield32 b)
{
  return a.data == b.data;
}

void bitfield32_set_bit(bitfield32 *bf, int bit)
{
  bf->data |= (1 << bit);
}

void bitfield32_unset_bit(bitfield32 *bf, int bit)
{
  bf->data &= ~(1 << bit);
}

bitfield32 bitfield32_or(bitfield32 a, bitfield32 b)
{
  bitfield32 ret = {0};
  ret.data = a.data | b.data;
  return ret;
}

void bitfield32_and(bitfield32 *a, bitfield32 b)
{
  a->data &= b.data;
}

typedef struct bitfield32_iter_st {
  bitfield32 bits;
  int pos;
} bitfield32_iter;

bitfield32_iter bitfield32_get_iter(bitfield32 bf)
{
  bitfield32_iter ret = {bf.data, 0};
  return ret;
}

int bitfield32_iter_next(bitfield32_iter *iter) {
  while (iter->pos < 32) {
    if (iter->bits.data & (1 << iter->pos)) {
      iter->pos ++;
      return iter->pos -1;
    }
    iter->pos ++;
  }
  return -1;
}

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

enum direction_e{TOP, LEFT, BOTTOM, RIGHT};
#define OPOSITE_DIRECTION(in) ((in + 2)%4)

static int dir_modifier_x[] = {0, -1, 0, 1};
static int dir_modifier_y[] = {-1, 0, 1, 0};
#define DIR_X(dir, x) ((x) + dir_modifier_x[dir])
#define DIR_Y(dir, y) ((y) + dir_modifier_y[dir])
static char *dir_names[] = {"TOP", "LEFT", "BOTTOM", "RIGHT"};

static uint32_t adler32(const void *buf, size_t buflength) {
  const uint8_t *buffer = (const uint8_t*)buf;

  uint32_t s1 = 1;
  uint32_t s2 = 0;

  for (size_t n = 0; n < buflength; n++) {
    s1 = (s1 + buffer[n]) % 65521;
    s2 = (s2 + s1) % 65521;
  }
  return (s2 << 16) | s1;
}

SDL_Surface *load_surface(char *data)
{
  SDL_Surface *tmp_surface = IMG_Load(data);
  SDL_Surface *surface = SDL_ConvertSurfaceFormat(tmp_surface, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(tmp_surface);
  return surface;
}

struct hash_list
{
  int count;
  uint32_t *values;
};

struct analyse_result {
  int tile_size;
  int tile_count;
  struct tiles {
    SDL_Rect rect;
    uint32_t bit;               /* bit-id */
    uint32_t hash;              /* hash value of tile */
    uint32_t hash_dir[4];       /* hash value for each side */
    struct hash_list allowed_neighbour_hashes[4];
    float weight;
  } *tiles;
  uint32_t map_width;
  uint32_t map_height;
  uint32_t *map;
  SDL_Texture *texture;
};


//shannon_entropy_for_square =
//  log(sum(weight)) -
//  (sum(weight * log(weight)) / sum(weight))
//
static float get_entropy(bitfield32 v, struct analyse_result *res)
{
  float sum = 0.0;
  float sum_weight_log = 0.0;
  bitfield32_iter i = bitfield32_get_iter(v);
  int id;
  while (-1 != (id = bitfield32_iter_next(&i))) {
    float weight = res->tiles[id].weight;
    sum += weight;
    sum_weight_log += weight * logf(weight);
  }
  return logf(sum) - sum_weight_log / sum;
}


int select_tile_based_on_weight(bitfield32 bits, struct analyse_result *result)
{
  struct weighted_element e[32];
  int cnt = 0;
  bitfield32_iter i = bitfield32_get_iter(bits);
  int id;
  while (-1 != (id = bitfield32_iter_next(&i))) {
    e[cnt].weight = result->tiles[id].weight;
    e[cnt].id = id;
    cnt += 1;
  }
  return select_by_weight(cnt, e);
}

void print_analyse_result(struct analyse_result *result)
{
  for(int i = 0; i < result->tile_count; ++i) {
    printf("tile %i (weight: %0.2f): hash:%x left: %x right: %x top: %x bottom: %x (%d, %d)\n",
        i,
        result->tiles[i].weight,
        result->tiles[i].hash,
        result->tiles[i].hash_dir[LEFT],
        result->tiles[i].hash_dir[RIGHT],
        result->tiles[i].hash_dir[TOP],
        result->tiles[i].hash_dir[BOTTOM],
        result->tiles[i].rect.x,
        result->tiles[i].rect.y);
    printf("rules:\n");
    for( int dir = 0; dir < 4; ++dir) {
      printf("%s:\n", dir_names[dir]);
      for(int n = 0; n < result->tiles[i].allowed_neighbour_hashes[dir].count; ++n) {
        printf(" %x\n", result->tiles[i].allowed_neighbour_hashes[dir].values[n]);
      }
    }

  }
  int pos = 0;
  for (int i = 0; i < result->map_height; ++i) {
    for(int x = 0; x < result->map_width; ++x) {
      printf("%u", result->map[pos++]);
    }
    printf("\n");
  }
}

void hash_list_add(struct hash_list *list, uint32_t hash)
{
  /* search for hash */
  for (int i = 0; i < list->count; ++i) {
    if (hash == list->values[i]) {
      /* found nothing to do */
      return;
    }
  }
  list->values = realloc(list->values, sizeof(*list->values) * (list->count + 1));
  list->values[list->count] = hash;
  list->count += 1;
}

int hash_list_find(struct hash_list *list, uint32_t hash)
{
  for (int i = 0; i < list->count;  ++i) {
    if (hash == list->values[i]) {
      return 1;
    }
  }
  return 0;
}

typedef struct hash_list_iter {
  int pos;
  struct hash_list *list;
} hash_list_iter;

void hash_list_iter_init(struct hash_list_iter *iter, struct hash_list *list)
{
  iter->pos = 0;
  iter->list = list;
}

int hash_list_iter_next(struct hash_list_iter *iter, uint32_t *hash)
{
  if (iter->pos >= iter->list->count) {
    return 0;
  }
  *hash = iter->list->values[iter->pos++];
  return 1;
}

uint32_t calculate_hash(enum direction_e direction, char *hash_buffer, int tile_size)
{
  char *v_buffer = NULL;
  if (direction == RIGHT || LEFT) {
    v_buffer = malloc(tile_size * 4);
  }
  int buffer_pos = 0;
  uint32_t ret = 0;
  switch (direction) {
    case TOP:
      ret = adler32(hash_buffer, tile_size * 4);
      break;
    case BOTTOM:
      ret = adler32(&hash_buffer[4 * tile_size * (tile_size - 1)], tile_size * 4);
      break;
    case LEFT:
      for (int i = 0; i < tile_size; ++i) {
        for (int b = 0; b < 4; ++b) {
          v_buffer[buffer_pos++] = hash_buffer[i * tile_size * 4 + b];
        }
      }
      ret = adler32(v_buffer, tile_size * 4);
      break;
    case RIGHT:
      for (int i = 0; i < tile_size; ++i) {
        for (int b = 0; b < 4; ++b) {
          v_buffer[buffer_pos++] = hash_buffer[i * tile_size * 4 + (tile_size - 1) * 4 + b];
        }
      }
      ret = adler32(v_buffer, tile_size * 4);
      break;
  }
  free(v_buffer);
  return ret;
}

int add_tile_to_index(struct analyse_result *ret, uint32_t hash, uint32_t directions[4], SDL_Rect rect)
{
  /* try to find tile by hash */
  for (int i = 0; i < ret->tile_count; ++i)
  {
    if (ret->tiles[i].hash == hash) {
      ret->tiles[i].weight += 1;
      return i;
    }
  }
  /* add new entry */
  struct tiles *new_entry = NULL;
  ret->tiles = realloc(ret->tiles, (ret->tile_count + 1)* sizeof(struct tiles));
  new_entry = &ret->tiles[ret->tile_count];
  ret->tile_count += 1;
  memset(new_entry, 0, sizeof(struct tiles));
  new_entry->rect = rect;
  new_entry->hash = hash;
  new_entry->weight = 1;
  for(int dir = 0; dir < 4; ++dir) {
    new_entry->hash_dir[dir] = directions[dir];
  }
  return ret->tile_count - 1;
}

void analyse_map(struct analyse_result *result)
{
  int w = result->map_width;
  int h = result->map_height;
  int pos = 0;
#define IN_RANGE(x, y) ((x) >= 0 && (x) < w && (y) >= 0 && (y) < h)
  for (int y = 0; y < result->map_height; ++y) {
    for (int x = 0; x < result->map_width; ++x) {
      for ( int dir = 0; dir < 4; ++dir) {
        uint32_t tile_id = result->map[y * result->map_width + x];
        int test_x = DIR_X(dir, x);
        int test_y = DIR_Y(dir, y);
        if (IN_RANGE(test_x, test_y)) {
          uint32_t test_tile_id = result->map[test_y * result->map_width + test_x];
          hash_list_add(&result->tiles[tile_id].allowed_neighbour_hashes[dir], result->tiles[test_tile_id].hash_dir[OPOSITE_DIRECTION(dir)]);
        }
      }
      pos += 1;
    }
  }
}

SDL_Renderer *glob_renderer = NULL;

struct analyse_result *analyse_image(char *name, int tile_size) {
  struct analyse_result *ret = calloc(1, sizeof(*ret));
  char *hash_buffer = calloc(1, tile_size * tile_size * 4);
  SDL_Surface *surface = load_surface(name);
  int surface_width = surface->w;
  int surface_height = surface->h;
  int tiles_w = surface->w / tile_size;
  int tiles_h = surface->h / tile_size;
  ret->map = calloc(1, sizeof(*ret->map) * tiles_w * tiles_h);
  ret->map_width = tiles_w;
  ret->map_height = tiles_h;
  ret->tile_size = tile_size;
  char *pixel_data = surface->pixels;
  int pos = 0;
  /* first split input image in tile_size chunks */
  for (int tiles_y = 0; tiles_y < tiles_h; ++tiles_y) {
    for (int tiles_x = 0; tiles_x < tiles_w; ++ tiles_x) {
      char *offset = &pixel_data[tiles_y * tile_size * surface_width * 4 + tiles_x * tile_size * 4];
      /* read tile_sizeXtile_size chunk at current position */
      for (int chunk_y = 0; chunk_y < tile_size; ++chunk_y) {
        memcpy(&hash_buffer[chunk_y * tile_size * 4], &offset[chunk_y * surface_width * 4], tile_size * 4);
      }
      uint32_t side_hashes[4];
      for (int dir = 0; dir < 4; ++dir) {
        side_hashes[dir] = calculate_hash(dir, hash_buffer, tile_size);
      }
      SDL_Rect rect = {tiles_x * tile_size, tiles_y * tile_size, tile_size, tile_size};
      ret->map[pos++] = add_tile_to_index(ret, adler32(hash_buffer, tile_size * tile_size * 4), side_hashes, rect);
    }
  }
  /* okay, after setting up our input map, it's time to create the ruleset */
  analyse_map(ret);
  /* create texture from surface */
  ret->texture = SDL_CreateTextureFromSurface(glob_renderer, surface);
  return ret;
}

void draw_tile(int x, int y, int tile_id, struct analyse_result *res)
{
  SDL_SetTextureBlendMode(res->texture, SDL_BLENDMODE_NONE);
  SDL_Rect rect = {x, y, res->tile_size, res->tile_size};
  SDL_RenderCopy(glob_renderer, res->texture, &res->tiles[tile_id].rect, &rect);
}

void draw_tile_based_on_weight(int x, int y, bitfield32 bits, struct analyse_result *res)
{
  float w[32] = {0.0};
  float sum = 0;
  int cnt = 0;
  int ids[32];
  bitfield32_iter iter = bitfield32_get_iter(bits);
  int id;
  while (-1 != (id = bitfield32_iter_next(&iter))) {
    w[cnt] += res->tiles[id].weight;
    ids[cnt] = id;
    sum += res->tiles[id].weight;
    cnt += 1;
  }
  SDL_SetTextureBlendMode(res->texture, SDL_BLENDMODE_BLEND);
  for (int i = 0; i < cnt; ++i) {
    SDL_SetTextureAlphaMod(res->texture, (w[i] / sum) * 255.0);
    SDL_Rect rect = {x, y, res->tile_size, res->tile_size};
    SDL_RenderCopy(glob_renderer, res->texture, &res->tiles[ids[i]].rect, &rect);
  }
}

void draw_input_map(struct analyse_result *result)
{
  for(int y = 0; y < result->map_height; ++y) {
    for (int x = 0; x < result->map_width; ++x) {
      SDL_Rect pos = {x * result->tile_size, y * result->tile_size, result->tile_size, result->tile_size};
      draw_tile(x * result->tile_size, y * result->tile_size, result->map[y * result->map_width + x], result);
    }
  }
}

typedef struct bitfield32_map {
  int map_width;
  int map_height;
  bitfield32 *map;
} bitfield_map;

void draw_map_with_weight(bitfield_map *map, struct analyse_result *result)
{
  for(int y = 0; y < map->map_height; ++y) {
    for (int x = 0; x < map->map_width; ++x) {
      SDL_Rect pos = {x * result->tile_size, y * result->tile_size, result->tile_size, result->tile_size};
      draw_tile_based_on_weight(x * result->tile_size, y * result->tile_size, map->map[y * map->map_width + x], result);
    }
  }
}

/* this binary-ands map position x/y with value and update neighbours recursively */
/* returns 
 * -1 tile out of range
 *  0 nothing changed
 *  1 value was modified
 */


/* what's happening here?
 *
 */
int update_map_with_rules(bitfield_map *map, int x, int y, struct analyse_result *res)
{
  if (x < 0 || x >= map->map_width || y < 0 || y >= map->map_height) {
    return -1;
  }
  bitfield32 *map_element = &map->map[y * map->map_width + x];
  bitfield32 old_value = *map_element;

  bitfield32_iter main_iter = bitfield32_get_iter(*map_element);
  int main_id;
  while (-1 != (main_id = bitfield32_iter_next(&main_iter))) {
    for (int dir = 0; dir < 4; ++dir) {
      int test_x = DIR_X(dir, x);
      int test_y = DIR_Y(dir, y);
      if (test_x >= 0 && test_x < map->map_width && test_y >= 0 && test_y < map->map_width) {
        bitfield32 *test_element = &map->map[map->map_width * test_y + test_x];
        bitfield32 allowed = {0};
        bitfield32_iter iter = bitfield32_get_iter(*test_element);
        int id;
        int match = 0;
        /* check all not-jet-forbidden ids */
        while (match == 0 && -1 != (id = bitfield32_iter_next(&iter))) {
          hash_list_iter hl_iter;
          hash_list_iter_init(&hl_iter, &res->tiles[id].allowed_neighbour_hashes[OPOSITE_DIRECTION(dir)]);
          uint32_t hash;
          while(hash_list_iter_next(&hl_iter, &hash)) {
            if (hash == res->tiles[main_id].hash_dir[dir]) {
              match = 1;
              break;
            }
          }
        }
        if (!match) {
          bitfield32_unset_bit(map_element, main_id);
          break;
        }
      }
    }
  }
  if (!bitfield32_cmp(old_value, *map_element)) {
    for (int dir = 0; dir < 4; ++dir) {
      int test_x = DIR_X(dir, x);
      int test_y = DIR_Y(dir, y);
      update_map_with_rules(map, test_x, test_y, res);
    }
    return 1;
  }
  return 0;
}

#include <time.h>

int main() {
  srand(time(NULL));
  int running =1 ;
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *glob_window = SDL_CreateWindow("Collapse",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      SCREEN_WIDTH, SCREEN_HEIGHT, 0);

  glob_renderer = SDL_CreateRenderer(glob_window, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  SDL_RenderSetLogicalSize(glob_renderer, 64, 64);

  struct analyse_result *test = analyse_image("test.png", 4);
  print_analyse_result(test);

  bitfield_map bf_map = {0};
  bf_map.map_width=10;
  bf_map.map_height=10;
  bf_map.map = calloc(1, 10 * 10);
  for(int i = 0; i < 10 * 10; ++i) {
    for(int b = 0; b < test->tile_count; ++b) {
      bitfield32_set_bit(&bf_map.map[i], b);
    }
  }
  int tt = 1;
  bitfield32_unset_bit(&bf_map.map[tt], 1);
  bitfield32_unset_bit(&bf_map.map[tt], 2);
  bitfield32_unset_bit(&bf_map.map[tt], 3);
  bitfield32_unset_bit(&bf_map.map[tt], 4);
  bitfield32_unset_bit(&bf_map.map[tt], 5);
  printf(" update rnuaire %d\n", update_map_with_rules(&bf_map, 0, 0, test));

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch(event.type) {
        case SDL_QUIT:
          running = 0;
          break;
      }
    }
    SDL_RenderClear(glob_renderer);
    //draw_input_map(test);
    draw_map_with_weight(&bf_map, test);
    SDL_RenderPresent(glob_renderer);
  }

  SDL_Quit();
#if 0
  struct weighted_element e[3] = {{.weight=2, .id=0}, {.weight=1, .id=1}, {.weight=1, .id=2}};
  int result[3] = {0,0,0};
  for (int i = 0; i < 100000; ++i) {
    result[select_by_weight(3, e)] ++;
  }
  for (int i = 0; i < 3; ++i) {
    printf("%i: %i\n", i, result[i]);
  }

  bitfield32 bf = {0};
  for (int i = 0; i <= 8; ++i) {
    bitfield32_set_bit(&bf, i);
  }
  bitfield32_iter iter = bitfield32_get_iter(bf);
  for (int i = bitfield32_iter_next(&iter); i != -1; i = bitfield32_iter_next(&iter)) {
    printf("-> bit %d set\n", i);
  }
  int result[9] = {0};
  for(int i = 0; i < 10000; ++i) {
    result[select_tile_based_on_weight(bf, test)] ++;
  }
  for(int i = 0; i < 9; ++i) {
    printf("%d: %d\n", i, result[i]);
  }
#endif
  return 0;
}
