#include "SDL_render.h"
#include "SDL_surface.h"
#include "SDL_surface.h"
#include <SDL.h>
#include <SDL_pixels.h>
#include <SDL_image.h>
#include <assert.h>

#define MAX_TILES 4096
#define BITS 64

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
  uint64_t data[MAX_TILES / BITS];
} bitfield32;


static int bitcount(uint32_t i)
{
     // Java: use >>> instead of >>
     // C or C++: use uint32_t
     i = i - ((i >> 1) & 0x55555555);
     i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
     return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

void bitfield32_update_bitcount(bitfield32 *b)
{
  b->bitcount = 0;
  for (int i = 0; i < MAX_TILES / BITS; ++i) {
    b->bitcount += bitcount(b->data[i] >> 32);
    b->bitcount += bitcount(b->data[i] & 0xFFFFFFFFUL);
  }
}

int bitfield32_get_bitcount(bitfield32 *bf)
{
  if (bf->bitcount_needs_update) {
    bitfield32_update_bitcount(bf);
    bf->bitcount_needs_update = 0;
  }
  return bf->bitcount;
}

int bitfield32_cmp(bitfield32 *a, bitfield32 *b)
{
  int ret = 1;
  for(int i= 0 ; i < MAX_TILES / BITS; ++i) {
    ret = (ret && a->data[i] == b->data[i]);
  }
  return ret;
}

void bitfield32_set_bit(bitfield32 *bf, int bit)
{
  assert(bit < MAX_TILES);
  bf->bitcount_needs_update = 1;
  bf->data[bit / BITS] |= (1UL << (bit % BITS));
}

void bitfield32_set_to(bitfield32 *bf, int bit)
{
  bf->bitcount_needs_update = 0;
  bf->bitcount = 1;
  memset(&bf->data, 0, sizeof(bf->data));
  bf->data[bit / BITS] = 1UL << (bit % BITS);
}

void bitfield32_unset_bit(bitfield32 *bf, int bit)
{
  bf->bitcount_needs_update = 1;
  bf->data[bit / BITS] &= ~(1UL << (bit % BITS));
}

static void bitfield32_and(bitfield32 *a, bitfield32 *b)
{
  a->bitcount_needs_update = 1;
  for(int i = 0; i < MAX_TILES / BITS; ++i) {
    a->data[i] &= b->data[i];
  }
}

void bitfield32_or(bitfield32 *a, bitfield32 *b)
{
  a->bitcount_needs_update = 1;
  for(int i = 0; i < MAX_TILES / BITS; ++i) {
    a->data[i] |= b->data[i];
  }
}

typedef struct bitfield32_iter_st {
  bitfield32 *bits;
  int pos;
  int bc;
} bitfield32_iter;

bitfield32_iter bitfield32_get_iter(bitfield32 *bf)
{
  bitfield32_iter ret = {bf, 0, bitfield32_get_bitcount(bf)};
  return ret;
}

int bitfield32_iter_next(bitfield32_iter *iter) {
  while (iter->bc && iter->pos < MAX_TILES) {
    if (iter->bits->data[iter->pos / BITS] & (1UL << (iter->pos % BITS))) {
      iter->bc -= 1;
      iter->pos += 1;
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
char *dir_names[] = {"TOP", "LEFT", "BOTTOM", "RIGHT"};


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

typedef bitfield32 (*allowed_neighbours_cache)[4];

struct analyse_result {
  int tile_size;
  int tile_count;
  struct tiles {
    uint32_t *tile_data;         /* bit data from surface */
    SDL_Rect rect;              /* position of tile */
    uint32_t bit;               /* bit-id */
    uint32_t hash;              /* hash value of tile */
    uint32_t hash_dir[4];       /* hash value for each side */
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
float get_entropy(bitfield32 *v, struct analyse_result *res)
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


int select_tile_based_on_weight(bitfield32 *bits, struct analyse_result *result)
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

  }
  int pos = 0;
  for (int i = 0; i < result->map_height; ++i) {
    for(int x = 0; x < result->map_width; ++x) {
      printf("%u", result->map[pos++]);
    }
    printf("\n");
  }
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

#if 0
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
#endif

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
  assert(0);
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

void tile_data_mirror_v(uint32_t *tile_data, int tile_size)
{
  uint32_t c;
  for(int y = 0; y< tile_size; ++y) {
    for (int x = 0; x < tile_size / 2; ++x) {
      c = tile_data[y * tile_size + x];
      tile_data[y * tile_size + x] = tile_data[y * tile_size + tile_size - x - 1];
      tile_data[y * tile_size + tile_size - x - 1] = c;
    }
  }
}

void tile_data_mirror_h(uint32_t *tile_data, int tile_size)
{
  uint32_t c[tile_size];
  for(int y= 0; y < tile_size / 2; ++y) {
    memcpy(c, &tile_data[y * tile_size], tile_size * sizeof(*tile_data));
    memcpy(&tile_data[y * tile_size], &tile_data[(tile_size - y - 1) * tile_size], tile_size * sizeof(*tile_data));
    memcpy(&tile_data[(tile_size - y - 1) * tile_size], c, tile_size * sizeof(*tile_data));
  }
}

void tile_data_rotate90(uint32_t *tile_data, int tile_size)
{
  uint32_t old[tile_size * tile_size];
  memcpy(old, tile_data, sizeof(old));
  for (int y = 0; y < tile_size; ++y) {
    for (int x = 0; x < tile_size; ++x) {
      tile_data[x * tile_size + (tile_size - y - 1)] = old[y * tile_size + x];
    }
  }
}

#define ANALYZE_FLAG_NO_Y_WRAP 1
#define ANALYZE_FLAG_NO_X_WRAP 2
#define ANALYZE_FLAG_DO_MIRROR_V 4
#define ANALYZE_FLAG_DO_MIRROR_H 8
#define ANALYZE_FLAG_DO_ROTATE 16
#define OUTPUT_FLAG_MAKE_SEAMLESS 32

struct analyse_result *overlap_analyse_image(char *name, int tile_size, int flags) {
  struct analyse_result *ret = calloc(1, sizeof(*ret));
  ret->tile_size = tile_size;
  SDL_Surface *surface = load_surface(name);
  int surface_width = surface->w;
  int surface_height = surface->h;
  uint32_t *tile_data = malloc(sizeof(uint32_t) * ret->tile_size * ret->tile_size);
  for (int y = 0; y < surface_height - (flags & ANALYZE_FLAG_NO_Y_WRAP)?tile_size:0 ; ++y) {
    for (int x = 0; x < surface_width - (flags & ANALYZE_FLAG_NO_X_WRAP)?tile_size:0; ++x) {
      overlap_get_tile_data(surface->pixels, surface_width, surface_height, tile_data, x, y, ret->tile_size, ret->tile_size);
      overlap_add_tile_to_index2(ret, tile_data);
      if (flags & ANALYZE_FLAG_DO_ROTATE) {
        tile_data_rotate90(tile_data, tile_size);
        overlap_add_tile_to_index2(ret, tile_data);
        tile_data_rotate90(tile_data, tile_size);
        overlap_add_tile_to_index2(ret, tile_data);
        tile_data_rotate90(tile_data, tile_size);
        overlap_add_tile_to_index2(ret, tile_data);
        tile_data_rotate90(tile_data, tile_size);
      }
      if (flags & ANALYZE_FLAG_DO_ROTATE && flags & ANALYZE_FLAG_DO_MIRROR_H && flags & ANALYZE_FLAG_DO_MIRROR_V) {
          tile_data_mirror_v(tile_data, tile_size);
          overlap_add_tile_to_index2(ret, tile_data);
          tile_data_rotate90(tile_data, tile_size);
          overlap_add_tile_to_index2(ret, tile_data);
          tile_data_rotate90(tile_data, tile_size);
          overlap_add_tile_to_index2(ret, tile_data);
          tile_data_rotate90(tile_data, tile_size);
          overlap_add_tile_to_index2(ret, tile_data);
      } else {
        if (flags & ANALYZE_FLAG_DO_MIRROR_V) {
          tile_data_mirror_v(tile_data, tile_size);
          overlap_add_tile_to_index2(ret, tile_data);
          /* reset mirroring */
          tile_data_mirror_v(tile_data, tile_size);
        }
        if (flags & ANALYZE_FLAG_DO_MIRROR_H) {
          tile_data_mirror_h(tile_data, tile_size);
          overlap_add_tile_to_index2(ret, tile_data);
          /* reset mirroring */
          tile_data_mirror_h(tile_data, tile_size);
        }
      }
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
    ret->tiles[i].rect.x = x * ret->tile_size;
    ret->tiles[i].rect.y = y * ret->tile_size;
    ret->tiles[i].rect.w = 1;
    ret->tiles[i].rect.h = 1;
  }
  ret->texture = SDL_CreateTextureFromSurface(glob_renderer, tmp_surface);
  SDL_FreeSurface(tmp_surface);

  return ret;
}


void draw_rect(int x, int y, int w, int h, int r, int g, int b)
{
  SDL_Rect rect = {x, y, w, h};
  SDL_SetRenderDrawColor(glob_renderer, r, g, b, 255);
  SDL_RenderDrawRect(glob_renderer, &rect);
  SDL_SetRenderDrawColor(glob_renderer, 0, 0, 0, 0);
}


void draw_tile(int x, int y, int tile_id, struct analyse_result *res)
{
  SDL_SetTextureBlendMode(res->texture, SDL_BLENDMODE_NONE);
  SDL_Rect rect = {x, y, res->tile_size, res->tile_size};
  SDL_RenderCopy(glob_renderer, res->texture, &res->tiles[tile_id].rect, &rect);
}

void draw_tile_based_on_weight(int x, int y, bitfield32 *bits, struct analyse_result *res)
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
    //SDL_Rect rect = {x, y, res->tile_size, res->tile_size};
    SDL_Rect rect = {x, y, 1, 1};
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
  allowed_neighbours_cache cache;
} bitfield32_map;

void draw_map_with_weight(bitfield32_map *map, struct analyse_result *result)
{
  for(int y = 0; y < map->map_height; ++y) {
    for (int x = 0; x < map->map_width; ++x) {
      draw_tile_based_on_weight(x, y, &map->map[y * map->map_width + x], result);
    }
  }
}

void split_pixel(uint32_t pixel, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
  *a = pixel & 0xff;
  pixel >>= 8;
  *b = pixel & 0xff;
  pixel >>= 8;
  *g = pixel & 0xff;
  pixel >>= 8;
  *r = pixel & 0xff;
}

uint32_t merge_pixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  uint32_t ret = 0;
  ret |= r;
  ret <<= 8;
  ret |= g;
  ret <<= 8;
  ret |= b;
  ret <<= 8;
  ret |= a;
  return ret;
}

void update_output_map(SDL_Surface *out, int x, int y, bitfield32_map *map, struct analyse_result *res)
{
  float w[MAX_TILES] = {0.0};
  float sum = 0;
  int cnt = 0;
  int ids[MAX_TILES];
  bitfield32_iter iter = bitfield32_get_iter(&map->map[map->map_width * y + x]);
  int id;
  while (-1 != (id = bitfield32_iter_next(&iter))) {
    w[cnt] += res->tiles[id].weight;
    ids[cnt] = id;
    sum += res->tiles[id].weight;
    cnt += 1;
  }
  uint8_t out_r=0;
  uint8_t out_g=0;
  uint8_t out_b=0;
  uint8_t out_a=0;
  for (int i = 0; i < cnt; ++i) {
    uint8_t tmp_r=0;
    uint8_t tmp_g=0;
    uint8_t tmp_b=0;
    uint8_t tmp_a=0;
    //SDL_SetTextureAlphaMod(res->texture, (w[i] / sum) * 255.0);
    split_pixel(res->tiles[ids[i]].tile_data[0], &tmp_r, &tmp_g, &tmp_b, &tmp_a);
    out_r += tmp_r * (float)w[i] / sum;
    out_g += tmp_g * (float)w[i] / sum;
    out_b += tmp_b * (float)w[i] / sum;
    out_a += tmp_a * (float)w[i] / sum;
  }
  uint32_t *map_data = out->pixels;
  map_data[y * out->w + x] = merge_pixel(out_r, out_g, out_b, out_a);
}

void update_allowed_neighbours_cache(struct bitfield32_map *map, int x, int y, struct analyse_result *res)
{
  for (int dir = 0; dir < 4; ++dir) {
    int id;
    bitfield32 allowed_tiles = {0};
    bitfield32_iter iter = bitfield32_get_iter(&map->map[map->map_width * y + x]);
    while (-1 != (id = bitfield32_iter_next(&iter))) {
      bitfield32_or(&allowed_tiles, &res->tiles[id].allowed_neighbours[dir]);
    }
    map->cache[map->map_width * y + x][dir] = allowed_tiles;
  }
}

void init_allowed_neighbours_cache(struct bitfield32_map *map, struct analyse_result *res)
{
  map->cache = calloc(1, sizeof(*map->cache) * map->map_width * map->map_height);
  for(int y = 0; y < map->map_height; ++y) {
    for (int x = 0; x < map->map_width; ++x) {
      update_allowed_neighbours_cache(map, x, y, res);
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
int update_map_with_rules(bitfield32_map *map, int x, int y, struct analyse_result *res, int from_dir, SDL_Surface *output_surface, int flags)
{
  /* wo dont evaluate any tiles that are out-of-map */
  if (flags & OUTPUT_FLAG_MAKE_SEAMLESS) {
    /* normalize map (wrap) */
    while (x < 0) {
      x += map->map_width;
    }
    while (y < 0) {
      y += map->map_height;
    }
    x %= map->map_width;
    y %= map->map_height;
  } else if (x < 0 || x >= map->map_width || y < 0 || y >= map->map_height) {
    return 0;
  }

  bitfield32 *map_element = &map->map[y * map->map_width + x];

  /* we dont modifie tiles when there are already collapsed (bitcount == 1) */
  if (bitfield32_get_bitcount(map_element) == 1) {
    return 0;
  }

  bitfield32 old_value = *map_element;

  for (int dir = 0; dir < 4; ++dir) {
    int test_x = DIR_X(dir, x);
    int test_y = DIR_Y(dir, y);
    if (flags & OUTPUT_FLAG_MAKE_SEAMLESS) {
      while (test_x < 0) {
        test_x += map->map_width;
      }
      while (test_y < 0) {
        test_y += map->map_height;
      }
      test_x %= map->map_width;
      test_y %= map->map_height;
    }
    if (test_x >= 0 && test_x < map->map_width && test_y >= 0 && test_y < map->map_height) {
      bitfield32_and(map_element, &map->cache[map->map_width * test_y + test_x][OPOSITE_DIRECTION(dir)]);
    }
  }
  if (!bitfield32_cmp(&old_value, map_element)) {
    /* add changed value to history */
    //bitfield32_map_history_add(&glob_history, x, y, old_value, 0);
    update_output_map(output_surface, x, y, map, res);
    update_allowed_neighbours_cache(map, x, y, res);
    switch (bitfield32_get_bitcount(map_element)) {
      case 0:
        /* ERROR condition */
        glob_error_cond.x = x;
        glob_error_cond.y = y;
        glob_error_cond.error = 1;
        printf("error condition\n");
        return -1;
      case 1:
        /* resolved */
        map_element->entropy = 0.0;
        break;
      default:
        /* recalculate entropy */
        /* XXX ugly XXX */
        map_element->entropy = get_entropy(map_element, res);
        break;
    }

    /* update neighbours */
    for (int dir = 0; dir < 4; ++dir) {
      if (dir == from_dir)
        continue;
      int test_x = DIR_X(dir, x);
      int test_y = DIR_Y(dir, y);
      if (-1 == update_map_with_rules(map, test_x, test_y, res, OPOSITE_DIRECTION(dir), output_surface, flags)) {
        return -1;
      }
    }
    return 1;
  }
  return 0;
}

void init_bitfield32_map(bitfield32_map *map, int w, int h, struct analyse_result *res, SDL_Surface *output_surface, int flags)
{
  map->map_width = w;
  map->map_height = h;
  map->map = calloc(1, sizeof(*map->map) * w * h);
  /* fill with all possibilities */
  bitfield32 tmp_v = {0};
  printf("initialize bitfield\n");
  for(int b = 0; b < res->tile_count; ++b) {
    bitfield32_set_bit(&tmp_v, b);
  }
  for (int i = 0; i < w * h; ++i) {
    map->map[i] = tmp_v;
  }
  printf("initialize neighbour-cache\n");
  init_allowed_neighbours_cache(map, res);
  /* initial update */
  printf("initial update\n");
  for(int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
     if (-1 == update_map_with_rules(map, x, y, res, -1, output_surface, flags)) {
       printf("error\n");
       return;
     }
    }
  }
  printf("initial entropy calc\n");
  /* calculate entropy */
  for(int i = 0; i < w * h; ++i) {
    /* XXX ugly XXX */
    map->map[i].entropy = get_entropy(&map->map[i], res);
  }
  printf("done\n");
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
#if 1
  if (argc < 5) {
    printf("Usage\ncollapse <image> <tile_size> <w> <h> [flags]\n");
    return -1;
  }
  char *image_name = argv[1];
  int tile_size = strtol(argv[2], NULL, 10);
  int map_w = 0;
  int map_h = 0;
  int flags = 0;
  map_w = strtol(argv[3], NULL, 10);
  map_h = strtol(argv[4], NULL, 10);
  for (int i=5 ; i < argc; ++i) {
    if (!strcasecmp(argv[i], "ROTATE")) {
      flags |= ANALYZE_FLAG_DO_ROTATE;
    } else if (!strcasecmp(argv[i], "MIRROR_V")) {
      flags |= ANALYZE_FLAG_DO_MIRROR_V;
    } else if (!strcasecmp(argv[i], "MIRROR_H")) {
      flags |= ANALYZE_FLAG_DO_MIRROR_H;
    } else if (!strcasecmp(argv[i], "NO_V_WRAP")) {
      flags |= ANALYZE_FLAG_NO_Y_WRAP;
    } else if (!strcasecmp(argv[i], "NO_H_WRAP")) {
      flags |= ANALYZE_FLAG_NO_X_WRAP;
    } else if (!strcasecmp(argv[i], "SEAMLESS")) {
      flags |= OUTPUT_FLAG_MAKE_SEAMLESS;
    } else {
      printf("illegal flag us: ROTATE MIRROR_V MIRROR_H NO_V_WRAP NO_H_WRAP SEAMLESS\n");
      exit(1);
    }
  }
  srand(time(NULL));
  int running =1 ;
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *glob_window = SDL_CreateWindow("Collapse",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      SCREEN_WIDTH, SCREEN_HEIGHT, 0);

  glob_renderer = SDL_CreateRenderer(glob_window, -1,
     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  SDL_RenderSetLogicalSize(glob_renderer, SCREEN_WIDTH / 8, SCREEN_HEIGHT / 8);

  //struct analyse_result *test = analyse_image(image_name, tile_size);
  struct analyse_result *overlap_result = overlap_analyse_image(image_name, tile_size, flags);
 // print_analyse_result(test);
 printf("tile_cnt = %d\n", overlap_result->tile_count);

  bitfield32_map bf_map = {0};
  SDL_Texture *output_texture = SDL_CreateTexture(glob_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, map_w, map_h);
  SDL_Surface *output_surface = SDL_CreateRGBSurfaceWithFormat(0, map_w, map_h, 32, SDL_PIXELFORMAT_RGBA8888);

  init_bitfield32_map(&bf_map, map_w, map_h, overlap_result, output_surface, flags);

  for( int tmp_y = 0; tmp_y < map_h; ++ tmp_y) {
    for (int tmp_x = 0; tmp_x < map_w; ++tmp_x) {
      update_output_map(output_surface, tmp_x, tmp_y, &bf_map, overlap_result);
    }
  }

  memset(&glob_history, 0, sizeof(glob_history));
  retry_cnt = 0;
  last_id = 0;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch(event.type) {
        case SDL_KEYDOWN:
          if (event.key.keysym.sym == 'q') {
            running = 0;
          } else if (event.key.keysym.sym == SDLK_SPACE) {
#if 0
          if (glob_error_cond.error) {
            /* reset history */
            bitfield32_map_history_rollback(&bf_map, &glob_history);
          } else {
            init_bitfield32_map(&bf_map, (SCREEN_WIDTH/scale)/test->tile_size,
               (SCREEN_HEIGHT/scale)/test->tile_size, test, flags);
          }
#endif
          free(bf_map.map);
          init_bitfield32_map(&bf_map, map_w, map_h, overlap_result, output_surface, flags);
          for( int tmp_y = 0; tmp_y < map_h; ++ tmp_y) {
            for (int tmp_x = 0; tmp_x < map_w; ++tmp_x) {
              update_output_map(output_surface, tmp_x, tmp_y, &bf_map, overlap_result);
            }
          }
          glob_error_cond.error = 0;
          memset(&glob_history, 0, sizeof(glob_history));
          retry_cnt = 0;
          last_id = 0;
          } else if (event.key.keysym.sym == 's') {
            SDL_SaveBMP(output_surface, "out.bmp");
          }
          break;
        case SDL_QUIT:
          running = 0;
          break;
      }
    }
   // SDL_SetRenderDrawColor(glob_renderer, 100, 100, 100, 255);
    SDL_RenderClear(glob_renderer);
     //SDL_RenderCopy(glob_renderer, overlap_result->texture, NULL, NULL);
    //draw_map_with_weight(&bf_map, overlap_result);
    // draw_input_map(test);
    int x = 0;
    int y = 0;
    if (!glob_error_cond.error && 0.0 < bitfield32_map_get_smales_entropy_pos (&bf_map, &x, &y)) {
      /* set last set tile */
      glob_error_cond.x0 = x;
      glob_error_cond.y0 = y;
      bitfield32 *bf = &bf_map.map[y * bf_map.map_width + x];

      //bitfield32_map_history_add(&glob_history, x, y, *bf, HISTORY_FLAG_SAVEPOINT);
      bitfield32_set_to(bf, select_tile_based_on_weight(bf, overlap_result));
      bf->entropy = 0.0;
      update_allowed_neighbours_cache(&bf_map, x, y, overlap_result);
      update_output_map(output_surface, x, y, &bf_map, overlap_result);
      /* update neighbours */
      for(int dir = 0; dir < 4; ++dir) {
         int test_x = DIR_X(dir, x);
         int test_y = DIR_Y(dir, y);
         if (-1 == update_map_with_rules(&bf_map, test_x, test_y, overlap_result, OPOSITE_DIRECTION(dir), output_surface, flags)) {
           break;
         }
      }
    }
    //draw_map_with_weight(&bf_map, overlap_result);
    SDL_UpdateTexture(output_texture, NULL, output_surface->pixels, 4 * output_surface->w);
    SDL_RenderCopy(glob_renderer, output_texture, NULL, NULL);
    SDL_RenderPresent(glob_renderer);
  }

  SDL_Quit();

#else
  bitfield32 bf = {0};
  for (int i = 0; i < 128; ++i) {
    bitfield32_set_bit(&bf, i);
  }
  for (int i = 0; i < 128; ++i) {
    bitfield32_unset_bit(&bf, i);
  }
  bitfield32_iter iter = bitfield32_get_iter(&bf);
  for (int i = bitfield32_iter_next(&iter); i != -1; i = bitfield32_iter_next(&iter)) {
    printf("-> bit %d set\n", i);
  }
  printf("bitc=%d\n", bitfield32_get_bitcount(&bf));
#endif
  return 0;
}
