// *** PART 1 ***

#include <cstdio>
#include <cstdint>
#include <limits>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

bool game_running = false;
int move_dir = 0;
bool fire_pressed = 0;

#define GL_ERROR_CASE(glerror)\
    case glerror: snprintf(error, sizeof(error), "%s", #glerror)

inline void gl_debug(const char *file, int line) {
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		char error[128];

		switch (err) {
			GL_ERROR_CASE(GL_INVALID_ENUM); break;
			GL_ERROR_CASE(GL_INVALID_VALUE); break;
			GL_ERROR_CASE(GL_INVALID_OPERATION); break;
			GL_ERROR_CASE(GL_INVALID_FRAMEBUFFER_OPERATION); break;
			GL_ERROR_CASE(GL_OUT_OF_MEMORY); break;
		default: snprintf(error, sizeof(error), "%s", "UNKNOWN_ERROR"); break;
		}

		fprintf(stderr, "%s - %s: %d\n", error, file, line);
	}
}

#undef GL_ERROR_CASE

void validate_shader(GLuint shader, const char *file = 0) {
	static const unsigned int BUFFER_SIZE = 512;
	char buffer[BUFFER_SIZE];
	GLsizei length = 0;

	glGetShaderInfoLog(shader, BUFFER_SIZE, &length, buffer);

	if (length > 0) {
		printf("Shader %d(%s) compile error: %s\n", shader, (file ? file : ""), buffer);
	}
}

bool validate_program(GLuint program) {
	static const GLsizei BUFFER_SIZE = 512;
	GLchar buffer[BUFFER_SIZE];
	GLsizei length = 0;

	glGetProgramInfoLog(program, BUFFER_SIZE, &length, buffer);

	if (length > 0) {
		printf("Program %d link error: %s\n", program, buffer);
		return false;
	}

	return true;
}


void error_callback(int error, const char* description)
{
	fprintf(stderr, "Error: %s\n", description);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	switch (key) {
	case GLFW_KEY_ESCAPE:
		if (action == GLFW_PRESS) game_running = false;
		break;
	case GLFW_KEY_RIGHT:
		if (action == GLFW_PRESS) move_dir += 1;
		else if (action == GLFW_RELEASE) move_dir -= 1;
		break;
	case GLFW_KEY_LEFT:
		if (action == GLFW_PRESS) move_dir -= 1;
		else if (action == GLFW_RELEASE) move_dir += 1;
		break;
	case GLFW_KEY_SPACE:
		if (action == GLFW_RELEASE) fire_pressed = true;
		break;
	default:
		break;
	}
}

/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
uint32_t xorshift32(uint32_t* rng)
{
	uint32_t x = *rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*rng = x;
	return x;
}

double random(uint32_t* rng)
{
	return (double)xorshift32(rng) / std::numeric_limits<uint32_t>::max();
}

struct Buffer
{
	size_t width, height;
	uint32_t* data;
};

struct Sprite
{
	size_t width, height;
	uint8_t* data;
};

struct Alien
{
	size_t x, y;
	uint8_t type;
};

struct Bullet
{
	size_t x, y;
	int dir;
};

struct Player
{
	size_t x, y;
	size_t life;
};

#define GAME_MAX_BULLETS 128

struct Game
{
	size_t width, height;
	size_t num_aliens;
	size_t num_bullets;
	Alien* aliens;
	Player player;
	Bullet bullets[GAME_MAX_BULLETS];
};

struct SpriteAnimation
{
	bool loop;
	size_t num_frames;
	size_t frame_duration;
	size_t time;
	Sprite** frames;
};

enum AlienType : uint8_t
{
	ALIEN_DEAD = 0,
	ALIEN_TYPE_A = 1,
	ALIEN_TYPE_B = 2,
	ALIEN_TYPE_C = 3
};

void buffer_clear(Buffer* buffer, uint32_t color)
{
	for (size_t i = 0; i < buffer->width * buffer->height; ++i)
	{
		buffer->data[i] = color;
	}
}

bool sprite_overlap_check(
	const Sprite& sp_a, size_t x_a, size_t y_a,
	const Sprite& sp_b, size_t x_b, size_t y_b
)
{
	// NOTE: For simplicity we just check for overlap of the sprite
	// rectangles. Instead, if the rectangles overlap, we should
	// further check if any pixel of sprite A overlap with any of
	// sprite B.
	if (x_a < x_b + sp_b.width && x_a + sp_a.width > x_b &&
		y_a < y_b + sp_b.height && y_a + sp_a.height > y_b)
	{
		return true;
	}

	return false;
}

void buffer_draw_sprite(Buffer* buffer, const Sprite& sprite, size_t x, size_t y, uint32_t color)
{
	for (size_t xi = 0; xi < sprite.width; ++xi)
	{
		for (size_t yi = 0; yi < sprite.height; ++yi)
		{
			if (sprite.data[yi * sprite.width + xi] &&
				(sprite.height - 1 + y - yi) < buffer->height &&
				(x + xi) < buffer->width)
			{
				buffer->data[(sprite.height - 1 + y - yi) * buffer->width + (x + xi)] = color;
			}
		}
	}
}

void buffer_draw_number(
	Buffer* buffer,
	const Sprite& number_spritesheet, size_t number,
	size_t x, size_t y,
	uint32_t color)
{
	uint8_t digits[64];
	size_t num_digits = 0;

	size_t current_number = number;
	do
	{
		digits[num_digits++] = current_number % 10;
		current_number = current_number / 10;
	} while (current_number > 0);

	size_t xp = x;
	size_t stride = number_spritesheet.width * number_spritesheet.height;
	Sprite sprite = number_spritesheet;
	for (size_t i = 0; i < num_digits; ++i)
	{
		uint8_t digit = digits[num_digits - i - 1];
		sprite.data = number_spritesheet.data + digit * stride;
		buffer_draw_sprite(buffer, sprite, xp, y, color);
		xp += sprite.width + 1;
	}
}

void buffer_draw_text(
	Buffer* buffer,
	const Sprite& text_spritesheet,
	const char* text,
	size_t x, size_t y,
	uint32_t color)
{
	size_t xp = x;
	size_t stride = text_spritesheet.width * text_spritesheet.height;
	Sprite sprite = text_spritesheet;
	for (const char* charp = text; *charp != '\0'; ++charp)
	{
		char character = *charp - 32;
		if (character < 0 || character >= 65) continue;

		sprite.data = text_spritesheet.data + character * stride;
		buffer_draw_sprite(buffer, sprite, xp, y, color);
		xp += sprite.width + 1;
	}
}

uint32_t rgb_to_uint32(uint8_t r, uint8_t g, uint8_t b)
{
	return (r << 24) | (g << 16) | (b << 8) | 255;
}