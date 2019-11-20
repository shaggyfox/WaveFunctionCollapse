#include <SDL.h>
#include <SDL_pixels.h>
#include <SDL_image.h>
#include <assert.h>

#define MAX_TILES 1024

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
  int ret = -1;
  if (cnt > 0) {
    float last = 0;
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
  int bitcount_needs_update;
  int bitcount;
  float entropy; /* used external */
  uint32_t data[MAX_TILES / 32];
} bitfield32;

void bitfield32_update_bitcount(bitfield32 *b)
{
  b->bitcount = 0;
  for (int i = 0; i < MAX_TILES; ++i) {
    if (b->data[i / 32] & (1u << (i % 32))) {
      b->bitcount += 1;
    }
  }
}

int bitfield32_get_bitcount(bitfield32 *bf)
{
  if (bf->bitcount_needs_update) {
    bitfield32_update_bitcount(bf);
  }
  return bf->bitcount;
}

int bitfield32_cmp(bitfield32 a, bitfield32 b)
{
  int ret = 1;
  for(int i= 0 ; i < MAX_TILES / 32; ++i) {
    ret = (ret && a.data[i] == b.data[i]);
  }
  return ret;
}

void bitfield32_set_bit(bitfield32 *bf, int bit)
{
  assert(bit < MAX_TILES);
  bf->bitcount_needs_update = 1;
  bf->data[bit /32] |= (1 << (bit % 32));
}

void bitfield32_set_to(bitfield32 *bf, int bit)
{
  bf->bitcount_needs_update = 0;
  bf->bitcount = 1;
  memset(&bf->data, 0, sizeof(bf->data));
  bf->data[bit / 32] = 1u << (bit % 32);
}

void bitfield32_unset_bit(bitfield32 *bf, int bit)
{
  bf->bitcount_needs_update = 1;
  bf->data[bit / 32] &= ~(1 << (bit % 32));
}

typedef struct bitfield32_iter_st {
  bitfield32 bits;
  int pos;
} bitfield32_iter;

bitfield32_iter bitfield32_get_iter(bitfield32 bf)
{
  bitfield32_iter ret = {bf, 0};
  return ret;
}

int bitfield32_iter_next(bitfield32_iter *iter) {
  while (iter->pos < MAX_TILES) {
    if (iter->bits.data[iter->pos / 32] & (1 << (iter->pos % 32))) {
      iter->pos ++;
      return iter->pos -1;
    }
    iter->pos ++;
  }
  return -1;
}

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

enum direction_e{TOP, LEFT, BOTTOM, RIGHT};
#define OPOSITE_DIRECTION(in) ((in + 2)%4)

static int dir_modifier_x[] = {0, -1, 0, 1};
static int dir_modifier_y[] = {-1, 0, 1, 0};
#define DIR_X(dir, x) ((x) + dir_modifier_x[dir])
#define DIR_Y(dir, y) ((y) + dir_modifier_y[dir])
static char *dir_names[] = {"TOP", "LEFT", "BOTTOM", "RIGHT"};


uint32_t murmur3_32(const uint8_t* key, size_t len, uint32_t seed)
{
	uint32_t h = seed;
	if (len > 3) {
		size_t i = len >> 2;
		do {
			uint32_t k;
			memcpy(&k, key, sizeof(uint32_t));
			key += sizeof(uint32_t);
			k *= 0xcc9e2d51;
			k = (k << 15) | (k >> 17);
			k *= 0x1b873593;
			h ^= k;
			h = (h << 13) | (h >> 19);
			h = h * 5 + 0xe6546b64;
		} while (--i);
	}
	if (len & 3) {
		size_t i = len & 3;
		uint32_t k = 0;
		do {
			k <<= 8;
			k |= key[i - 1];
		} while (--i);
		k *= 0xcc9e2d51;
		k = (k << 15) | (k >> 17);
		k *= 0x1b873593;
		h ^= k;
	}
	h ^= len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}


static uint32_t adler32(char* key, size_t len)
{
  return murmur3_32((const uint8_t *)key, len, 123456);
}

/* 0
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
*/

SDL_Surface *load_surface(char *data)
{
  SDL_Surface *tmp_surface = IMG_Load(data);
  SDL_Surface *surface = SDL_ConvertSurfaceFormat(tmp_surface, SDL_PIXELFORMAT_RGBA8888, 0);
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
    uint32_t *tile_data;         /* bit data from surface */
    SDL_Rect rect;              /* position of tile */
    uint32_t bit;               /* bit-id */
    uint32_t hash;              /* hash value of tile */
    uint32_t hash_dir[4];       /* hash value for each side */
    struct hash_list allowed_neighbour_hashes[4];
    bitfield32 allowed_neighbours[4];
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
float get_entropy(bitfield32 v, struct analyse_result *res)
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
  struct weighted_element e[MAX_TILES];
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
  if (direction & (RIGHT | LEFT)) {
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

int overlap_add_tile_to_index2(struct analyse_result *ret, uint32_t *tile_data)
{
  /* hash data */
  uint32_t hash = murmur3_32((uint8_t*)tile_data, ret->tile_size * ret->tile_size * 4, 1234);
  /* try to find tile (by hash) */
  for (int i = 0; i < ret->tile_count; ++i) {
    if (hash == ret->tiles[i].hash) {
      /* if hash matches increment weight on tile */
      ret->tiles[i].weight += 1;
      return i;
    }
  }
  /* else add new element */
  ret->tiles =  realloc(ret->tiles, (ret->tile_count +1) * sizeof(*ret->tiles));
  struct tiles *new_entry = &ret->tiles[ret->tile_count++];
  memset(new_entry, 0, sizeof(*new_entry));
  new_entry->hash = hash;
  new_entry->weight = 1;
  new_entry->tile_data = malloc(sizeof(uint32_t) * ret->tile_size * ret->tile_size);
  memcpy(new_entry->tile_data, tile_data, sizeof(uint32_t) * ret->tile_size * ret->tile_size);
  return ret->tile_count - 1;
}

int overlap_tiles_attach(uint32_t *tile_a, uint32_t *tile_b, enum direction_e dir, int tile_size)
{
  switch (dir) {
    case TOP:
      return !memcmp(tile_a, &tile_b[tile_size], tile_size * (tile_size - 1)* sizeof(*tile_a));
      break;
    case LEFT:
      for(int i = 0; i < tile_size; ++i) {
        if (memcmp(&tile_a[i * tile_size], &tile_b[i * tile_size + 1], (tile_size - 1) * sizeof(*tile_a))) {
          return 0;
        }
      }
      return 1;
      break;
    case BOTTOM:
      return !memcmp(&tile_a[tile_size], tile_b, tile_size * (tile_size - 1) * sizeof(*tile_a));
      break;
    case RIGHT:
      for(int i = 0; i < tile_size; ++i) {
        if (memcmp(&tile_a[i * tile_size + 1], &tile_b[i * tile_size], (tile_size - 1) * sizeof(*tile_a))) {
          return 0;
        }
      }
      return 1;
      break;
  }
  return -1;
}

void overlap_get_tile_data(uint32_t *data, int data_w, int data_h, uint32_t *ret, int x, int y, int width, int height)
{
  for(int tile_y = 0; tile_y < height; ++tile_y) {
    for (int tile_x = 0; tile_x < width; ++tile_x) {
      ret[tile_y * width + tile_x] = data[((y+tile_y) % data_h) * data_w + ((x+tile_x) % data_w)];
    }
  }
}

void overlap_analyse_tiles(struct analyse_result *res)
{
  for(int tile_a = 0; tile_a < res->tile_count; ++tile_a) {
    for( int tile_b = 0; tile_b < res->tile_count; ++tile_b) {
      for(int dir = 0; dir < 4; ++dir) {
        if (overlap_tiles_attach(res->tiles[tile_a].tile_data, res->tiles[tile_b].tile_data, dir, res->tile_size)) {
          bitfield32_set_bit(&res->tiles[tile_a].allowed_neighbours[dir], tile_b);
        }
      }
    }
  }
}

SDL_Renderer *glob_renderer = NULL;

struct analyse_result *overlap_analyse_image(char *name, int tile_size) {
  struct analyse_result *ret = calloc(1, sizeof(*ret));
  ret->tile_size = tile_size;
  SDL_Surface *surface = load_surface(name);
  int surface_width = surface->w;
  int surface_height = surface->h;
  uint32_t *tile_data = malloc(sizeof(uint32_t) * ret->tile_size * ret->tile_size);
  for (int y = 0; y < surface_height; ++y) {
    for (int x = 0; x < surface_width; ++x) {
      overlap_get_tile_data(surface->pixels, surface_width, surface_height, tile_data, x, y, ret->tile_size, ret->tile_size);
      overlap_add_tile_to_index2(ret, tile_data);
    }
  }
  overlap_analyse_tiles(ret);
  uint32_t rmask = 0xff000000;
  uint32_t gmask = 0x00ff0000;
  uint32_t bmask = 0x0000ff00;
  uint32_t amask = 0x000000ff;
  int bpp = 0;
  SDL_PixelFormatEnumToMasks (SDL_PIXELFORMAT_RGBA8888, &bpp, &rmask, &gmask, &bmask, &amask);
  int surface_w = ceilf(sqrtf(ret->tile_count));
  int surface_h = surface_w;
  SDL_Surface *tmp_surface = SDL_CreateRGBSurface(0,
      surface_w * ret->tile_size,
      surface_h *  ret->tile_size,
      32,
      rmask, gmask, bmask, amask);
  for (int i = 0; i < ret->tile_count; ++i) {
    SDL_Surface *tile_surface =
      SDL_CreateRGBSurfaceFrom(ret->tiles[i].tile_data,ret->tile_size, ret->tile_size, 32, ret->tile_size * 4, rmask, gmask, bmask, amask);
    int x = i % surface_w;
    int y = i / surface_w;
    SDL_Rect src_rect = {0, 0, ret->tile_size, ret->tile_size};
    SDL_Rect dst_rect = {x * ret->tile_size, y * ret->tile_size, ret->tile_size, ret->tile_size};
    SDL_BlitSurface(tile_surface, &src_rect, tmp_surface, &dst_rect);

  }

  printf("%d %d\n", tmp_surface->w, tmp_surface->h);
  ret->texture = SDL_CreateTextureFromSurface(glob_renderer, tmp_surface);
  SDL_FreeSurface(tmp_surface);

  return ret;
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

void draw_rect(int x, int y, int w, int h, int r, int g, int b)
{
  SDL_Rect rect = {x, y, w, h};
  SDL_SetRenderDrawColor(glob_renderer, r, g, b, 255);
  SDL_RenderDrawRect(glob_renderer, &rect);
  SDL_SetRenderDrawColor(glob_renderer, 0, 0, 0, 0);
}


struct analyse_result *analyse_image(char *name, int tile_size) {
  struct analyse_result *ret = calloc(1, sizeof(*ret));
  char *hash_buffer = calloc(1, tile_size * tile_size * 4);
  SDL_Surface *surface = load_surface(name);
  int surface_width = surface->w;
  //int surface_height = surface->h;
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

#if 0
        /* DEBUG */
      printf("%x = ", adler32(hash_buffer, tile_size * tile_size * 4));
        for(int i= 0; i < tile_size * tile_size * 4; ++i) {
          printf("%02x", hash_buffer[i]);
        }
        printf("\n");
        /* DEBUG */
#endif
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
  float w[MAX_TILES] = {0.0};
  float sum = 0;
  int cnt = 0;
  int ids[MAX_TILES];
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
      draw_tile(x * result->tile_size, y * result->tile_size, result->map[y * result->map_width + x], result);
    }
  }
}

typedef struct bitfield32_map {
  int map_width;
  int map_height;
  bitfield32 *map;
} bitfield32_map;

void draw_map_with_weight(bitfield32_map *map, struct analyse_result *result)
{
  for(int y = 0; y < map->map_height; ++y) {
    for (int x = 0; x < map->map_width; ++x) {
      draw_tile_based_on_weight(x * result->tile_size, y * result->tile_size, map->map[y * map->map_width + x], result);
    }
  }
}


#define MAX_HISTORY 10000
#define HISTORY_FLAG_IN_USE 1
#define HISTORY_FLAG_SAVEPOINT 2
typedef struct bitfield32_history_element_st bitfield32_history_element;
struct bitfield32_history_element_st {
  bitfield32_history_element *next;
  bitfield32_history_element *prev;
  int flags;
  int x;
  int y;
  int id;
  bitfield32 value;
};

typedef struct {
  int pos;
  int cnt;
  int last_id;
  bitfield32_history_element e[MAX_HISTORY];
  bitfield32_history_element *first;
  bitfield32_history_element *last;
} bitfield32_history;

#include <unistd.h>

int bitfield32_map_history_rollback(bitfield32_map *map, bitfield32_history *history)
{
  int ret = -1;
  while (history->last) {
  //printf("->%d\n", history->cnt);
    int has_flag = history->last->flags & HISTORY_FLAG_SAVEPOINT;
    int x = history->last->x;
    int y = history->last->y;
    int id = history->last->id;
    bitfield32 v = history->last->value;
    history->last->flags = 0; /* reset flags */
    history->cnt -= 1;
    map->map[map->map_width * y + x] = v;
    history->last = history->last->prev;
    if (has_flag) {
      history->last_id -= 1;
      ret = id;
      break;
    }
  }
  if (!history->last) {
    history->first = NULL;
  }
  return ret;
}

int bitfield32_map_history_add(bitfield32_history *history, int x, int y, bitfield32 value, int flags)
{
  bitfield32_history_element *e = NULL;
  if (history->cnt == MAX_HISTORY) {
    e = history->first;
    history->first = e->next;
    history->first->prev = NULL;
    e->next = NULL;
  } else {
    history->cnt += 1;
    //printf("-> ++ %d %d\n", history->cnt, flags);
    while (!e) {
      bitfield32_history_element *test =  &history->e[(history->pos ++)%MAX_HISTORY];
      if (!(test->flags & HISTORY_FLAG_IN_USE)) {
        e = test;
      }
    }
  }
  e->x = x;
  e->y = y;
  e->value = value;
  e->flags = flags | HISTORY_FLAG_IN_USE;
  if (flags & HISTORY_FLAG_SAVEPOINT) {
    e->id = history->last_id ++;
  }
  e->prev = history->last;
  if (history->last) {
    history->last->next = e;
  }
  history->last = e;
  if (!history->first) {
    history->first = e;
  }
  return 0;
}

bitfield32_history glob_history = {0};
int retry_cnt = 0;
int last_id = 0;
int rollback_cnt = 0;
#define MAX_RETRIES 10

/* this binary-ands map position x/y with value and update neighbours recursively */
/* returns
 * -1 tile out of range
 *  0 nothing changed
 *  1 value was modified
 */

struct error_condition {
  int error;
  int x;
  int y;
  int x0;
  int y0;
} glob_error_cond = {0};

/* what's happening here?
 *
 */
int update_map_with_rules(bitfield32_map *map, int x, int y, struct analyse_result *res)
{
  /* wo dont evaluate any tiles that are out-of-map */
  if (x < 0 || x >= map->map_width || y < 0 || y >= map->map_height) {
    return 0;
  }

  bitfield32 *map_element = &map->map[y * map->map_width + x];

  /* we dont modifie tiles when there are already collapsed (bitcount == 1) */
  if (bitfield32_get_bitcount(map_element) == 1) {
    return 0;
  }

  bitfield32 old_value = *map_element;

  bitfield32_iter main_iter = bitfield32_get_iter(*map_element);
  int main_id;
  while (-1 != (main_id = bitfield32_iter_next(&main_iter))) {
    for (int dir = 0; dir < 4; ++dir) {
      int test_x = DIR_X(dir, x);
      int test_y = DIR_Y(dir, y);
      if (test_x >= 0 && test_x < map->map_width && test_y >= 0 && test_y < map->map_height) {
        bitfield32 *test_element = &map->map[map->map_width * test_y + test_x];
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
    /* add changed value to history */
    bitfield32_map_history_add(&glob_history, x, y, old_value, 0);
    switch (bitfield32_get_bitcount(map_element)) {
      case 0:
        /* ERROR condition */
        glob_error_cond.x = x;
        glob_error_cond.y = y;
        glob_error_cond.error = 1;
        return -1;
      case 1:
        /* resolved */
        map_element->entropy = 0.0;
        break;
      default:
        /* recalculate entropy */
        /* XXX ugly XXX */
        map_element->entropy = get_entropy(*map_element, res);
        break;
    }

    /* update neighbours */
    for (int dir = 0; dir < 4; ++dir) {
      int test_x = DIR_X(dir, x);
      int test_y = DIR_Y(dir, y);
      if (-1 == update_map_with_rules(map, test_x, test_y, res)) {
        return -1;
      }
    }
    return 1;
  }
  return 0;
}

void init_bitfield32_map(bitfield32_map *map, int w, int h, struct analyse_result *res)
{
  map->map_width = w;
  map->map_height = h;
  map->map = calloc(1, sizeof(*map->map) * w * h);
  /* fill with all possibilities */
  for (int i = 0; i < w * h; ++i) {
    for(int b = 0; b < res->tile_count; ++b) {
      bitfield32_set_bit(&map->map[i], b);
    }
  }
  /* initial update */
  for(int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      update_map_with_rules(map, x, y, res);
    }
  }
  /* calculate entropy */
  for(int i = 0; i < w * h; ++i) {
    /* XXX ugly XXX */
    map->map[i].entropy = get_entropy(map->map[i], res);
  }
}

float bitfield32_map_get_smales_entropy_pos(bitfield32_map *map, int *out_x, int *out_y)
{
  float smalest = 0.0;
  for (int y = 0; y <  map->map_height; ++y) {
    for (int x = 0; x < map->map_width; ++x) {
      bitfield32 *b = &map->map[y * map->map_width + x];
      if (bitfield32_get_bitcount(b) > 1) {
        if (smalest == 0.0 || b->entropy < smalest) {
          *out_x = x;
          *out_y = y;
          smalest = b->entropy;
        }
      }
    }
  }
  return smalest;
}


#include <time.h>
int main(int argc, char **argv) {
  int scale = 4;
  if (argc < 3) {
    printf("Usage\ncollapse <image> <tile_size>\n");
    return -1;
  }
  char *image_name = argv[1];
  int tile_size = strtol(argv[2], NULL, 10);
  if (argc >= 4) {
    scale = strtol(argv[3], NULL, 10);
  }
  srand(time(NULL));
  int running =1 ;
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *glob_window = SDL_CreateWindow("Collapse",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      SCREEN_WIDTH, SCREEN_HEIGHT, 0);

  glob_renderer = SDL_CreateRenderer(glob_window, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  SDL_RenderSetLogicalSize(glob_renderer, SCREEN_WIDTH / scale, SCREEN_HEIGHT / scale);

  struct analyse_result *test = analyse_image(image_name, tile_size);
  struct analyse_result *overlap_result = overlap_analyse_image(image_name, tile_size);
 // print_analyse_result(test);

  bitfield32_map bf_map = {0};

  init_bitfield32_map(&bf_map, (SCREEN_WIDTH/scale)/test->tile_size,
      (SCREEN_HEIGHT/scale)/test->tile_size, test);
  memset(&glob_history, 0, sizeof(glob_history));
  retry_cnt = 0;
  last_id = 0;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch(event.type) {
        case SDL_KEYDOWN:
#if 0
          if (glob_error_cond.error) {
            /* reset history */
            bitfield32_map_history_rollback(&bf_map, &glob_history);
          } else {
            init_bitfield32_map(&bf_map, (SCREEN_WIDTH/scale)/test->tile_size,
               (SCREEN_HEIGHT/scale)/test->tile_size, test);
          }
#endif
          free(bf_map.map);
          init_bitfield32_map(&bf_map, (SCREEN_WIDTH/scale)/test->tile_size,
              (SCREEN_HEIGHT/scale)/test->tile_size, test);
          glob_error_cond.error = 0;
          memset(&glob_history, 0, sizeof(glob_history));
          retry_cnt = 0;
          last_id = 0;
          break;
        case SDL_QUIT:
          running = 0;
          break;
      }
    }
    SDL_SetRenderDrawColor(glob_renderer, 100, 100, 100, 255);
    SDL_RenderClear(glob_renderer);
    SDL_RenderCopy(glob_renderer, overlap_result->texture, NULL, NULL);
    // draw_input_map(test);
#if 0
    int x, y;
    if (!glob_error_cond.error && 0.0 < bitfield32_map_get_smales_entropy_pos (&bf_map, &x, &y)) {
      /* set last set tile */
      glob_error_cond.x0 = x;
      glob_error_cond.y0 = y;
      bitfield32 *bf = &bf_map.map[y * bf_map.map_width + x];

      bitfield32_map_history_add(&glob_history, x, y, *bf, HISTORY_FLAG_SAVEPOINT);
      bitfield32_set_to(bf, select_tile_based_on_weight(*bf, test));
      /* update neighbours */
      for(int dir = 0; dir < 4; ++dir) {
         int test_x = DIR_X(dir, x);
         int test_y = DIR_Y(dir, y);
         if (-1 == update_map_with_rules(&bf_map, test_x, test_y, test)) {
           break;
         }
      }
    }
    draw_map_with_weight(&bf_map, test);
    if (glob_error_cond.error) {
      draw_rect(glob_error_cond.x0 * test->tile_size, glob_error_cond.y0 * test->tile_size,  test->tile_size, test->tile_size,
          0, 255, 0);
      draw_rect(glob_error_cond.x * test->tile_size, glob_error_cond.y * test->tile_size,  test->tile_size, test->tile_size,
          255, 0, 0);
      int id = bitfield32_map_history_rollback(&bf_map, &glob_history);
      assert(id >= 0);
      if (id <= last_id) {
        retry_cnt += 1;
        if (retry_cnt > 10) {
          printf("left states =%d\n", glob_history.cnt);
          printf("last_id = %d (%d)\n", last_id, id);
          printf("roll-back-back: %i\n", rollback_cnt);
          for(int i = 0; i < rollback_cnt; ++i) {
            bitfield32_map_history_rollback(&bf_map, &glob_history);
          }
          retry_cnt = 0;
          rollback_cnt += 1;
        }
      } else {
        last_id = id;
        retry_cnt = 0;
        rollback_cnt = 0;
      }
      glob_error_cond.error = 0;
    }
#endif
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
  for (int i = 0; i <= 43; ++i) {
    bitfield32_set_bit(&bf, i);
  }
  bitfield32_iter iter = bitfield32_get_iter(bf);
  for (int i = bitfield32_iter_next(&iter); i != -1; i = bitfield32_iter_next(&iter)) {
    printf("-> bit %d set\n", i);
  }
  int result[43] = {0};
  for(int i = 0; i < 10000; ++i) {
    result[select_tile_based_on_weight(bf, test)] ++;
  }
  for(int i = 0; i < 43; ++i) {
    printf("%d: %d\n", i, result[i]);
  }
#endif
  return 0;
}
