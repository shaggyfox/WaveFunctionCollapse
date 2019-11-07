#include <SDL.h>
#include <SDL_image.h>

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

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

struct analyse_result {
  int tile_size;
  int tile_count;
  struct tiles {
    uint32_t hash;
    uint32_t neighbours[8];
    float weight;
  } *tiles;
};

struct analyse_result *analyse_image(char *name, int tile_size) {
  struct analyse_result *ret = calloc(1, sizeof(*ret));
  char *hash_buffer = calloc(1, tile_size * tile_size * 4);
  SDL_Surface *surface = load_surface(name);
  int surface_width = surface->w;
  int surface_height = surface->h;
  int tiles_w = surface->w / tile_size;
  int tiles_h = surface->h / tile_size;
  char *pixel_data = surface->pixels;
  for (int tiles_y = 0; tiles_y < tiles_h; ++tiles_y) {
    for (int tiles_x = 0; tiles_x < tiles_w; ++ tiles_x) {
      printf("x=%d y=%d\n", tiles_x * tile_size, tiles_y * tile_size);
    }
  }
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
