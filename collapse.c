#include <SDL.h>
#include <SDL_image.h>

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

enum direction_e{TOP, LEFT, BOTTOM, RIGHT};
#define OPOSITE_DIRECTION(in) ((in + 2)%4)

static int dir_modifier_x[] = {0, -1, 0, 1};
static int dir_modifier_y[] = {-1, 0, 1, 0};
#define DIR_X(dir, x) ((x) + dir_modifier_x[dir])
#define DIR_Y(dir, y) ((y) + dir_modifier_y[dir])
static char *dir_names[] = {"TOP", "LEFT", "BOTTOM", "RIGHT"};

uint32_t adler32(const void *buf, size_t buflength) {
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
};


void print_analyse_result(struct analyse_result *result)
{
  for(int i = 0; i < result->tile_count; ++i) {
    printf("tile %i: hash:%x left: %x right: %x top: %x bottom: %x (%d, %d)\n",
        i,
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
  struct tiles *found = NULL;
  for (int i = 0; i < ret->tile_count; ++i)
  {
    if (ret->tiles[i].hash == hash) {
      found = &ret->tiles[i];
      return i;
    }
  }
  /* add new entry */
  ret->tiles = realloc(ret->tiles, (ret->tile_count + 1)* sizeof(struct tiles));
  found = &ret->tiles[ret->tile_count];
  ret->tile_count += 1;
  memset(found, 0, sizeof(struct tiles));
  found->rect = rect;
  found->hash = hash;
  for(int dir = 0; dir < 4; ++dir) {
    found->hash_dir[dir] = directions[dir];
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
  return ret;
}

int main() {
  int running =1 ;
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *glob_window = SDL_CreateWindow("Collapse",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      SCREEN_WIDTH, SCREEN_HEIGHT, 0);

  SDL_Renderer *glob_renderer = SDL_CreateRenderer(glob_window, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);


  struct analyse_result *test = analyse_image("test.png", 8);
  print_analyse_result(test);

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
    SDL_RenderPresent(glob_renderer);
  }

  SDL_Quit();

  return 0;
}
