#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include <locale.h>
#include <GL/glew.h>

#ifdef __APPLE__
#include <SDL2.h>
#else
#include <fenv.h>

/* For some reason I have to define this prototype myself, (defining _GNU_SOURCE before
 * including fenv.h doesn't help.)
 */
extern int feenableexcept(int excepts); 

#include <SDL.h>
#endif

#include "mtwist.h"
#include "vertex.h"
#include "snis_graph.h"
#include "graph_dev.h"
#include "quat.h"
#include "material.h"
#include "entity.h"
#include "mesh.h"
#include "stl_parser.h"
#include "mathutils.h"
#include "snis_typeface.h"
#include "opengl_cap.h"
#include "build_info.h"
#include "png_utils.h"
#include "turret_aimer.h"
#include "open-simplex-noise.h"
#include "replacement_assets.h"
#include "snis_xwindows_hacks.h"

#define DEFAULT_FOV (30.0 * M_PI / 180.0)
#define MIN_FOV (15.0 * M_PI / 180.0)
#define MAX_FOV (95.0 * M_PI / 180.0)
#define FPS 60

static int SCREEN_WIDTH = 1600;		/* window width, in pixels */
static int SCREEN_HEIGHT = 900;	/* window height, in pixels */
static int screen_offset_x;
static int screen_offset_y;
#define ASPECT_RATIO (SCREEN_WIDTH/(float)SCREEN_HEIGHT)
static int window_manager_can_constrain_aspect_ratio = 0;
static float original_aspect_ratio;
static int time_to_set_window_size = 0;
static int real_screen_width;
static int real_screen_height;

static int wireframe = 0;
static int oldwireframe = 0;
static int autospin_initialized = 0;
static int autospin = 0;
static int autospin_stop_frame = -1;
static int draw_atmosphere = 1;
static int frame_counter = 0;
static int periodic_snapshots = 0;
static int snapshot_number = 0;
static int use_alpha_by_normal = 0;
static float alpha_by_normal_invert = 0.0;
static char *planetname = NULL;
static char *normalmapname = NULL;
static char *diffusename = NULL;
static char *cubemapname = NULL;
static char *cylinder_albedo = NULL;
static char cylindrical_axis = 0; /* 0 = x, 1 = z */
static char *cylinder_emit = NULL;
static char *modelfile = NULL;
static char *thrustfile = NULL;
static char *program;
static char *skyboxfile = NULL;
static char *turret_model = NULL;
static char *turret_base_model = NULL;
union quat autorotation; 
static int icosahedron_subdivision = 4;
static int trap_nans = 0;
#define LASER_VELOCITY 200.0

static int display_frame_stats = 1;

static struct mesh *target_mesh;
static struct mesh *turret_base_mesh;
static struct mesh *atmosphere_mesh;
static struct mesh *light_mesh;
static struct material planet_material;
static struct material green_phaser_material;
static struct material thrust_material;
static struct material atmosphere_material;
float atmosphere_brightness = 0.5;
static struct material cyl_albedo;
static struct material diffuse_material;
static struct material alpha_by_normal;
static struct material warpgate_effect_material;
static int planet_mode = 0;
static int cubemap_mode = 0;
static int burst_rod_mode = 0;
static int thrust_mode = 0;
static int turret_mode = 0;
static int no_skybox = 0;
static int warpgate_mode = 0;
static int black_and_white = 0;
static char *replacement_assets_file = NULL;
static struct replacement_asset replacement_asset = { 0 };
static int reload_shaders = 0;
static float FOV = DEFAULT_FOV;

static char *maybe_replace_asset(char *asset)
{
	return replacement_asset_lookup(asset, &replacement_asset);
}

static struct mesh *snis_read_model(char *path)
{
	float minx, miny, minz, maxx, maxy, maxz;
	struct mesh *m;
	char *filename;

	filename = maybe_replace_asset(path);
	m = read_mesh(filename);
	if (!m) {
		fprintf(stderr, "mesh_viewer: bad mesh file '%s'\n", filename);
		return NULL;
	}
	mesh_aabb(m, &minx, &miny, &minz, &maxx, &maxy, &maxz);
	printf("%s aabb = (%f,%f,%f), (%f, %f, %f)\n",
		filename, minx, miny, minz, maxx, maxy, maxz);
	return m;
}

static char *help_text =
	"MESH VIEWER\n\n"
	"  CONTROLS\n"
	"  * MOUSE RIGHT-CLICK DRAG TO ROTATE MODEL\n"
	"  * MOUSE SCROLL WHEEL TO ZOOM\n"
	"  * MOUSE CONTROL-RIGHT-CLICK DRAG TO ROTATE LIGHT\n"
	"  * ESC TO EXIT VIEWER\n"
	"  - a TOGGLE ATMOSPHERE RENDERING (PLANET MODE ONLY)\n"
	"  - ] MAKE ATMOSPHERE BRIGHTER\n"
	"  - [ MAKE ATMOSPHERE DIMMER\n"
	"  - p TAKE A SNAPSHOT\n"
	"  - f TOGGLE PERIODIC SNAPSHOTS\n"
	"  - s TOGGLE AUTO-SPIN MODE\n"
	"  - n TURN AUTO_SPIN ON FOR 10 FRAMES\n"
	"  - + SPEED UP RATE OF SPIN\n"
	"  - - SLOW DOWN RATE OF SPIN\n"
	"PRESS F1 TO EXIT HELP\n";

static void draw_help_text(const char *text)
{
	int line = 0;
	int i, y = 70;
	char buffer[256];
	int buflen = 0;
	int helpmodeline = 0;

	strcpy(buffer, "");

	i = 0;
	do {
		if (text[i] == '\n' || text[i] == '\0') {
			if (line >= helpmodeline && line < helpmodeline + 20) {
				buffer[buflen] = '\0';
				sng_abs_xy_draw_string(buffer, TINY_FONT, 60, y);
				y += 19;
				strcpy(buffer, "");
				buflen = 0;
				line++;
				if (text[i] == '\0')
					break;
				i++;
				continue;
			} else {
				if (line >= helpmodeline + 20)
					break;
			}
		}
		buffer[buflen++] = text[i++];
	} while (1);
}

static void draw_help_screen()
{
	sng_set_foreground(BLACK);
	sng_current_draw_rectangle(1, 50, 50, SCREEN_WIDTH - 50, SCREEN_HEIGHT - 100);
	sng_set_foreground(GREEN);
	sng_current_draw_rectangle(0, 50, 50, SCREEN_WIDTH - 50, SCREEN_HEIGHT - 100);
	draw_help_text(help_text);
}

static void quit(int code)
{
	SDL_Quit();

	exit(code);
}

static int helpmode;
static SDL_Window *screen;
static SDL_Renderer *renderer;

static void adjust_spinning(float speed_factor)
{
	float x, y, z, a;

	quat_to_axis(&autorotation, &x, &y, &z, &a);
	a = a * speed_factor;
	quat_init_axis(&autorotation, x, y, z, a);
}

static void take_picture(char *filename)
{
	unsigned char *buffer;
	int width, height;
	graph_dev_grab_framebuffer(&buffer, &width, &height);
	printf("Saved snapshot: %s %dx%d\n", filename, width, height);
	png_utils_write_png_image(filename, buffer, width, height, 1, 1);
	free(buffer);
}

static void take_snapshot(void)
{
	char filename[100];
	sprintf(filename, "mesh-viewer-snapshot-%05d.png", snapshot_number);
	snapshot_number++;
	take_picture(filename);
}

static void take_periodic_snapshot(void)
{
	if (!periodic_snapshots)
		return;
	if ((frame_counter % 10) != 0)
		return;
	take_snapshot();
}

static void distort_the_mesh(int distort)
{
	static struct mesh *undistorted = NULL;
	static struct osn_context *osn = NULL;

	if (!osn)
		open_simplex_noise(7737422399LL, &osn);

	if (!undistorted) {
		undistorted = mesh_duplicate(target_mesh);
	} else {
		mesh_free(target_mesh);
		target_mesh = mesh_duplicate(undistorted);
	}
	if (distort)
		mesh_distort(target_mesh, 0.25, osn);
	mesh_set_average_vertex_normals(target_mesh);
	mesh_graph_dev_init(target_mesh);
}

static void adjust_fov(float factor)
{
	if (factor > 1.0 && FOV < MAX_FOV) {
		FOV *=	factor;
		return;
	}
	if (factor < 1.0 && FOV > MIN_FOV)
		FOV *= factor;
}

static void handle_key_down(SDL_Keysym *keysym)
{
	static int fullscreen = 0;

	switch (keysym->sym) {
	case SDLK_F1:
		helpmode = !helpmode;
		break;
	case SDLK_F10:
		reload_shaders = 1;
		break;
	case SDLK_ESCAPE:
		quit(0);
		break;
	case SDLK_F11:
		fullscreen = !fullscreen;
		SDL_SetWindowFullscreen(screen, fullscreen * SDL_WINDOW_FULLSCREEN_DESKTOP);
		break;
	case SDLK_PAUSE:
		display_frame_stats = (display_frame_stats + 1) % 3;
		break;
	case SDLK_n:
		autospin = 1;
		autospin_stop_frame = frame_counter + 10;
		break;
	case SDLK_r:
		oldwireframe = wireframe;
		wireframe = !wireframe;
		break;
	case SDLK_s:
		autospin = !autospin;
		break;
	case SDLK_a:
		draw_atmosphere = !draw_atmosphere;
		break;
	case SDLK_f:
		periodic_snapshots = !periodic_snapshots;
		break;
	case SDLK_p:
		take_snapshot();
		break;
	case SDLK_MINUS:
	case SDLK_KP_MINUS:
		adjust_spinning(0.9);
		break;
	case SDLK_PLUS:
	case SDLK_KP_PLUS:
		adjust_spinning(1.1);
		break;
	case SDLK_LEFTBRACKET:
		atmosphere_brightness -= 0.05;
		if (atmosphere_brightness < 0.0)
			atmosphere_brightness = 0.0;
		break;
	case SDLK_RIGHTBRACKET:
		atmosphere_brightness += 0.05;
		if (atmosphere_brightness > 1.0)
			atmosphere_brightness = 1.0;
		break;
	case SDLK_d:
		distort_the_mesh(1);
		break;
	case SDLK_u:
		distort_the_mesh(0);
		break;
	case SDLK_0:
		adjust_fov(1.02);
		break;
	case SDLK_9:
		adjust_fov(0.98);
		break;
	default:
		break;
	}

}

static volatile int isDragging;
static volatile int isDraggingLight;
static union quat last_lobby_orientation = IDENTITY_QUAT_INITIALIZER;
static union quat last_light_orientation = IDENTITY_QUAT_INITIALIZER;
static union quat lobby_orientation = IDENTITY_QUAT_INITIALIZER;
static union quat light_orientation = IDENTITY_QUAT_INITIALIZER;
static union quat turret_orientation = IDENTITY_QUAT_INITIALIZER;
static union quat turret_base_orientation = IDENTITY_QUAT_INITIALIZER;
static float desired_lobby_zoom = 255;
static float lobby_zoom = 255;

#define MOUSE_HISTORY 5
static volatile float lastx[MOUSE_HISTORY], lasty[MOUSE_HISTORY];
static volatile int last = -1;
static volatile int lastcount = 0;
static int main_da_motion_notify(int x, int y)
{
	float cx, cy, lx, ly;
	union vec3 v1, v2;
	union quat rotation;

	if (!isDragging) {
		lastcount = 0;
	} else {
		if (lastcount < MOUSE_HISTORY) {
			last++;
			lastx[last % MOUSE_HISTORY] =
				2.0 * (((float) x / (float) real_screen_width) - 0.5);
			lasty[last % MOUSE_HISTORY] =
				2.0 * (((float) y / (float) real_screen_height) - 0.5);
			lastcount++;
			return 0;
		}
		lastcount++;
		lx = lastx[(last + 1) % MOUSE_HISTORY];
		ly = lasty[(last + 1) % MOUSE_HISTORY];
		last = (last + 1) % MOUSE_HISTORY;
		cx = 2.0 * (((float) x / (float) real_screen_width) - 0.5);
		cy = 2.0 * (((float) y / (float) real_screen_height) - 0.5);
		lastx[last] = cx;
		lasty[last] = cy;

		v1.v.z = 0;
		v1.v.y = 0;
		v1.v.x = -1.0;
		v2.v.z = cx - lx;
		v2.v.y = cy - ly;
		v2.v.x = -1.0;

		quat_from_u2v(&rotation, &v1, &v2, 0);
		if (isDraggingLight) {
			quat_mul(&light_orientation, &rotation, &last_light_orientation);
			quat_normalize_self(&light_orientation);
			last_light_orientation = light_orientation;
		} else {
			quat_mul(&lobby_orientation, &rotation, &last_lobby_orientation);
			last_lobby_orientation = lobby_orientation;
			v2.v.z /= 3.0;
			v2.v.y /= 3.0;
			quat_from_u2v(&autorotation, &v1, &v2, 0);
			autospin_initialized = 1;
		}
	}
	return 0;
}

void do_autospin(void)
{
	if (!autospin || !autospin_initialized)
		return;
	quat_mul(&lobby_orientation, &autorotation, &last_lobby_orientation);
	last_lobby_orientation = lobby_orientation;
}

static int main_da_button_press(int button, __attribute__((unused)) int x, __attribute__((unused)) int y)
{
	if (button == 3) {
		/* start drag */
		isDragging = 1;

		SDL_Keymod mod = SDL_GetModState();
		isDraggingLight = mod & (KMOD_LCTRL | KMOD_RCTRL);

		last = -1.0f;
		lastcount = 0;
	}
	return 0;
}

static int main_da_scroll(int direction)
{
	if (direction)
		desired_lobby_zoom = lobby_zoom + 10;
	else
		desired_lobby_zoom = lobby_zoom - 10;

	if (desired_lobby_zoom < 0)
		desired_lobby_zoom = 0;
	if (desired_lobby_zoom > 255)
		desired_lobby_zoom = 255;
	return 1;
}

static int main_da_button_release(int button, int x, int y)
{
	if (button == 4) {
		main_da_scroll(1);
		return 1;
	} else if (button == 5) {
		main_da_scroll(0);
		return 1;
	}
	last = -1;
	lastcount = 0;

	if (button == 1 && display_frame_stats) {
		if (graph_dev_graph_dev_debug_menu_click(x, y))
			return 1;
	}

	if (button == 3) {
		if (isDragging) {
			/* end drag */
			isDragging = 0;
			isDraggingLight = 0;
		}
	}

	return 1;
}

static int sdl_button_to_int(int sdl_button)
{
	switch (sdl_button) {
	case SDL_BUTTON_LEFT:
		return 1;
	case SDL_BUTTON_MIDDLE:
		return 2;
	case SDL_BUTTON_RIGHT:
		return 3;
	}
	return 0;
}

/* This is used to manually resize the window if the window manager is
 * not capable of constraining the aspect ratio.
 */
static void maybe_resize_window(SDL_Window *window)
{
	if (window_manager_can_constrain_aspect_ratio)
		return;
	if (!time_to_set_window_size)
		return;
	SDL_SetWindowSize(window, real_screen_width, real_screen_height);
	time_to_set_window_size = 0;
}

/* This is used to manually constrain the aspect ratio and dimensions of the window
 * if the window manager isn't capable of constraining the aspect ratio. The window
 * dimensions are set here but the window is not actually resized here because this
 * gets called from the SDL event hanlder, process_events() below. Apparently you
 * can't get good results from SDL_SetWindowSize() if you call it while
 * SDL_PollEvents() is returning non-zero. (I don't know why that is.)
 */
static void constrain_aspect_ratio(int *w, int *h)
{
	float new_aspect_ratio = (float) *w / (float) *h;

	/* a = w / h */
	/* w = a * h */
	/* h = w / a */

	if (new_aspect_ratio < original_aspect_ratio) {
		*h = (int) ((float) *w / original_aspect_ratio);
		*w = (int) (original_aspect_ratio * (float) *h);
	} else if (new_aspect_ratio > original_aspect_ratio) {
		*w = (int) (original_aspect_ratio * (float) *h);
		*h = (int) ((float) *w / original_aspect_ratio);
	}
}

static void process_events()
{
	/* Our SDL event placeholder. */
	SDL_Event event;

	/* Grab all the events off the queue. */
	while (SDL_PollEvent(&event)) {

		switch (event.type) {
		case SDL_KEYDOWN:
			/* Handle key presses. */
			handle_key_down(&event.key.keysym);
			break;
		case SDL_QUIT:
			/* Handle quit requests (like Ctrl-c). */
			quit(0);
			break;
		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
				event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {

				/* We need to update real_screen_width and real_screen_height, and
				 * call sng_set_screen_size(). Additionally if the window manager can't
				 * constrain the aspect ratio, we need to do that too. and set a flag so
				 * the window can be resized once we're out of the event loop.
				 */
				if (window_manager_can_constrain_aspect_ratio) { /* E.g. X11 */
					SDL_GL_GetDrawableSize(screen,
						&real_screen_width, &real_screen_height);
				} else {
					/* We have to constrain the aspect ratio manually on e.g. Wayland.
					 * This works *almost* as well as having the window manager do it.
					 * The only thing that isn't quite as good is that the user is able
					 * to stretch the window to non-conforming sizes, and once they
					 * release the mouse button, the window will snap to the correct size.
					 */
					int width, height;

					SDL_GL_GetDrawableSize(screen, &width, &height);
					constrain_aspect_ratio(&width, &height);
					/* If statement here to keep from creating an infinite event loop */
					if (real_screen_width != width || real_screen_height != height) {
						real_screen_width = width;
						real_screen_height = height;

						/* I would call SDL_SetWindowSize() here, but it doesn't seem
						 * to work. Perhaps because it's being called from the event
						 * handler?  This does not quite make sense to me, but anyway,
						 * so we set this flag and do it later.
						 */
						time_to_set_window_size = 1; /* see maybe_resize_window() */
					}
				}
				sng_set_screen_size(real_screen_width, real_screen_height);
			}
#if 0
			if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
				real_screen_width = event.window.data1;
				real_screen_height = event.window.data2;

				SDL_SetWindowSize(screen, real_screen_width, real_screen_height);
				sng_set_screen_size(real_screen_width, real_screen_height);
			}
#endif
			break;
		case SDL_MOUSEWHEEL: {
				if (event.wheel.y > 0)
					main_da_button_release(4, event.button.x, event.button.y);
				else if (event.wheel.y < 0)
					main_da_button_release(5, event.button.x, event.button.y);
			}
			break;
		case SDL_MOUSEBUTTONDOWN: {
				int button = sdl_button_to_int(event.button.button);
				if (button > 0)
					main_da_button_press(button, event.button.x, event.button.y);
			}
			break;
		case SDL_MOUSEBUTTONUP: {
				int button = sdl_button_to_int(event.button.button);
				if (button > 0)
					main_da_button_release(button, event.button.x, event.button.y);
			}
			break;
		case SDL_MOUSEMOTION:
			main_da_motion_notify(event.motion.x, event.motion.y);
			break;
		}
	}

}

static void check_modes(void)
{
	/* modes are mutually exclusive, ensure at most one is selected. */
	int sum = planet_mode + cubemap_mode + burst_rod_mode + thrust_mode + turret_mode + warpgate_mode;
	if (turret_mode) {
		if (!turret_model) {
			fprintf(stderr,
				"mesh_viewer: turret mode selected, but no turret model specified.\n");
			exit(1);
		}
		if (!turret_base_model) {
			fprintf(stderr,
				"mesh_viewer: turret mode selected, but no turret base model specified.\n");
			exit(1);
		}
	}
	if (sum <= 1)
		return;
	fprintf(stderr, "mesh_viewer: burstrod, cubemap, planet, thrust, turret and warpgate\n");
	fprintf(stderr, "             modes are mutually exclusive.\n");
	exit(1);
}

static void scroll_warpgate_texture(void)
{
	float u1, u2;

	if (!warpgate_mode)
		return;

	/* Scroll through the warp gate effect texture */
	u1 = (float) (frame_counter & 0x0ff) / (float) 0x0ff;
	u2 = u1 + 0.1;
	if (u2 > 1.0)
		u2 = u2 - 1.0;
	warpgate_effect_material.warp_gate_effect.u1 = u1;
	warpgate_effect_material.warp_gate_effect.u2 = u2;
}

#define FRAME_INDEX_MAX 10

static void draw_screen()
{
	static double last_frame_time;
	static int frame_index;
	static float frame_rates[FRAME_INDEX_MAX];
	static float frame_times[FRAME_INDEX_MAX];

	double start_time = time_now_double();

	if (black_and_white)
		glClearColor(1.0, 1.0, 1.0, 1.0);
	else
		glClearColor(0.0, 0.0, 0.0, 0.0);

	graph_dev_start_frame();

	sng_set_foreground(WHITE);
	sng_abs_xy_draw_string("F1 FOR HELP", NANO_FONT, SCREEN_WIDTH - 100, 10);

	static struct entity_context *cx;
	if (!cx)
		cx = entity_context_new(50, 50);

	if (wireframe != oldwireframe) {
		oldwireframe = wireframe;
		if (wireframe)
			set_renderer(cx, WIREFRAME_RENDERER | BLACK_TRIS);
		else
			set_renderer(cx, FLATSHADING_RENDERER);
	}

	if (lobby_zoom != desired_lobby_zoom) {
		lobby_zoom += 0.1 * (desired_lobby_zoom - lobby_zoom);
	}
	float r = target_mesh->radius / tan(FOV / 3.0); /* 50% size for middle zoom */
	float r_cam = r * lobby_zoom / 255.0;
	
	camera_set_parameters(cx, 0.1f, r * 2.2, SCREEN_WIDTH, SCREEN_HEIGHT, FOV);
	camera_set_pos(cx, r_cam, 0, 0);
	camera_look_at(cx, 0, 0, 0);
	camera_assign_up_direction(cx, 0, 1, 0);

	union vec3 light_pos = { { 1.01 * r, 0, 0 } };
	quat_rot_vec_self(&light_pos, &light_orientation);
	set_lighting(cx, light_pos.v.x, light_pos.v.y, light_pos.v.z);

	calculate_camera_transform(cx);

	struct entity *e = add_entity(cx, target_mesh, 0, 0, 0, WHITE);
	struct entity *turret_base_entity = NULL;
	struct entity *ae = NULL;
	if (planet_mode) {
		update_entity_material(e, &planet_material);
		if (draw_atmosphere) {
			ae = add_entity(cx, atmosphere_mesh, 0, 0, 0, WHITE);
			update_entity_scale(ae, 1.03);
			update_entity_material(ae, &atmosphere_material);
		}
	} else if (cubemap_mode) {
		update_entity_material(e, &planet_material);
	} else if (burst_rod_mode) {
		update_entity_material(e, &green_phaser_material);
	} else if (thrust_mode) {
		update_entity_material(e, &thrust_material);
	} else if (cylinder_albedo) {
		update_entity_material(e, &cyl_albedo);
	} else if (diffusename) {
		update_entity_material(e, &diffuse_material);
	} else if (use_alpha_by_normal) {
		update_entity_material(e, &alpha_by_normal);
	} else if (warpgate_mode) {
		update_entity_material(e, &warpgate_effect_material);
		update_entity_scale(e, 1.0);
		scroll_warpgate_texture();
	}
	if (!turret_mode) {
		update_entity_orientation(e, &lobby_orientation);
	} else {
		union quat new_turret_orientation, new_turret_base_orientation;
		union vec3 aim = { { 1.0, 0.0, 0.0 } };
		struct turret_params tparams;
		int aim_is_good = 0;

		tparams.elevation_lower_limit = -30.0 * M_PI / 180.0;
		tparams.elevation_upper_limit = 90.0 * M_PI / 180.0;
		tparams.azimuth_lower_limit = -3.0 * M_PI; /* no limit */
		tparams.azimuth_upper_limit = 3.0 * M_PI; /* no limit */
		tparams.elevation_rate_limit = 1.0 * M_PI / 180.0; /* 60 degrees/sec at 60Hz */
		tparams.azimuth_rate_limit = 1.0 * M_PI / 180.0; /* 60 degrees/sec at 60Hz */

		/* Aim turret at the light source */
		quat_rot_vec_self(&aim, &light_orientation);

		turret_aim(aim.v.x, aim.v.y, aim.v.z, 0.0, 0.0, 0.0,
			&lobby_orientation, /* turret rest orientation */
			&turret_orientation, /* current turret orientation */
			&tparams, &new_turret_orientation, &new_turret_base_orientation,
			&aim_is_good);

		turret_orientation = new_turret_orientation;
		turret_base_orientation = new_turret_base_orientation;
		turret_base_entity = add_entity(cx, turret_base_mesh, 0, 0, 0, WHITE);
		update_entity_orientation(e, &turret_orientation);
		update_entity_orientation(turret_base_entity, &turret_base_orientation);
	}

	if (isDraggingLight) {
		union vec3 light_dir = { { 10.75 * r_cam, 0, 0 } };
		quat_rot_vec_self(&light_dir, &light_orientation);
		sng_set_foreground(WHITE);
		render_line(cx, light_dir.v.x, light_dir.v.y, light_dir.v.z, 0, 0, 0);

		e = add_entity(cx, light_mesh, light_dir.v.x, light_dir.v.y, light_dir.v.z, WHITE);
	} else {
		e = add_entity(cx, light_mesh, light_pos.v.x, light_pos.v.y, light_pos.v.z, WHITE);
	}

	if (!no_skybox)
		render_skybox(cx);
	render_entities(cx);

	remove_all_entity(cx);

	if (helpmode)
		draw_help_screen(0);

	if (display_frame_stats > 0) {
		float avg_frame_rate = 0;
		float avg_frame_time = 0;
		int i;
		for (i = 0; i < FRAME_INDEX_MAX; i++) {
			avg_frame_rate += frame_rates[i];
			avg_frame_time += frame_times[i];
		}
		avg_frame_rate /= (float)FRAME_INDEX_MAX;
		avg_frame_time /= (float)FRAME_INDEX_MAX;

		sng_set_foreground(WHITE);
		char stat_buffer[30];
		if (avg_frame_rate != 0)
			sprintf(stat_buffer, "fps %5.2f", 1.0 / avg_frame_rate);
		else
			sprintf(stat_buffer, "fps ?????.??");
		sng_abs_xy_draw_string(stat_buffer, NANO_FONT, 2, 10);
		sprintf(stat_buffer, "t %0.2f ms", avg_frame_time * 1000.0);
		sng_abs_xy_draw_string(stat_buffer, NANO_FONT, 92, 10);
	}
	if (display_frame_stats > 1)
		graph_dev_display_debug_menu_show();

	graph_dev_end_frame();

	glFinish();

	/*
	 * Swap the buffers. This this tells the driver to
	 * render the next frame from the contents of the
	 * back-buffer, and to set all rendering operations
	 * to occur on what was the front-buffer.
	 *
	 * Double buffering prevents nasty visual tearing
	 * from the application drawing on areas of the
	 * screen that are being updated at the same time.
	 */
	SDL_GL_SwapWindow(screen);

	if (display_frame_stats > 0) {
		double end_time = time_now_double();

		frame_rates[frame_index] = start_time - last_frame_time;
		frame_times[frame_index] = end_time - start_time;
		frame_index = (frame_index + 1) % FRAME_INDEX_MAX;
		last_frame_time = start_time;
	}
	take_periodic_snapshot();
	frame_counter++;
	if (frame_counter == autospin_stop_frame) {
		autospin_stop_frame = -1;
		autospin = 0;
	}
}

static unsigned int load_cubemap_textures(int is_inside, int linear_colorspace, char *filenameprefix)
{
	/*
	 * SNIS wants skybox textures in six files named like this:
	 * blah0.png
	 * blah1.png
	 * blah2.png
	 * blah3.png
	 * blah4.png
	 * blah5.png
	 *
	 * and those images should be laid out like this:
	 *
	 *  +------+
	 *  |  4   |
	 *  |      |
	 *  +------+------+------+------+
	 *  |  0   |  1   |  2   |  3   |
	 *  |      |      |      |      |
	 *  +------+------+------+------+
	 *  |  5   |
	 *  |      |
	 *  +------+
	 *
	 *  Why?  No reason other than that's how I did it in cosmic-space-boxinator
	 *  See: https://github.com/smcameron/cosmic-space-boxinator
	 *
	 *  Opengl does it a bit differenty, so we have the arbitrariness you see below.
	 */
	int i;
	char filename[6][PATH_MAX + 1];

	for (i = 0; i < 6; i++)
		sprintf(filename[i], "%s%d.png", filenameprefix, i);

	return graph_dev_load_cubemap_texture(is_inside, linear_colorspace,
			maybe_replace_asset(filename[1]),
			maybe_replace_asset(filename[3]),
			maybe_replace_asset(filename[4]),
			maybe_replace_asset(filename[5]),
			maybe_replace_asset(filename[0]),
			maybe_replace_asset(filename[2]));
}

static void setup_skybox(char *skybox_prefix)
{
	const char *asset_dir = "share/snis/textures";
	int i;
	char filename[6][PATH_MAX + 1];

	if (!skyboxfile) {
		for (i = 0; i < 6; i++)
			sprintf(filename[i], "%s/%s%d.png", asset_dir, skybox_prefix, i);
	} else {
		for (i = 0; i < 6; i++)
			sprintf(filename[i], "%s%d.png", skyboxfile, i);
	}

	graph_dev_load_skybox_texture(maybe_replace_asset(filename[3]),
					maybe_replace_asset(filename[1]),
					maybe_replace_asset(filename[4]),
					maybe_replace_asset(filename[5]),
					maybe_replace_asset(filename[0]),
					maybe_replace_asset(filename[2]));
}

__attribute__((noreturn)) void usage(char *program)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "%s -p <planet-texture> [ -i icosohedrion-subdivision] [ -n <normal-map-texture> ]\n",
			program);
	fprintf(stderr, "           [ -r replacement_assets.txt ]\n");
	fprintf(stderr, " %s -m <mesh-file> [ -c cubemap-texture- ]\n", program);
	fprintf(stderr, " %s --burstrod\n", program);
	fprintf(stderr, " %s --thrust <image-file>\n", program);
	fprintf(stderr, " %s --turret <turret-model> --turretbase <turret-base-model>\n", program);
	fprintf(stderr, " %s --cylindrical <cylindrical-texture-map>\n", program);
	fprintf(stderr, " %s -m <mesh-file> --emittance <cylindrical-emittance-map>\n", program);
	fprintf(stderr, " %s --warpgate\n", program);
	fprintf(stderr, "             (emittance requires --cylindrical to be set)\n");
	exit(-1);
}

static void process_int_option(char *option_name, char *option_value, int *value)
{
	int tmp;

	if (sscanf(option_value, "%d", &tmp) == 1) {
		*value = tmp;
	} else {
		fprintf(stderr, "Bad %s option '%s'\n", option_name, option_value);
		usage(program);
	}
}

static struct option long_options[] = {
	{ "black-and-white", no_argument, NULL, 'W' },
	{ "model", required_argument, NULL, 'm' },
	{ "turretbase", required_argument, NULL, 'B' },
	{ "cubemap", required_argument, NULL, 'c' },
	{ "cylindrical", required_argument, NULL, 'C' },
	{ "zcylindrical", required_argument, NULL, 'Z' },
	{ "ycylindrical", required_argument, NULL, 'Y' },
	{ "emittance", required_argument, NULL, 'e' },
	{ "help", no_argument, NULL, 'h' },
	{ "planetmode", required_argument, NULL, 'p' },
	{ "icosahedron", required_argument, NULL, 'i' },
	{ "invertalphabynormal", no_argument, NULL, 'I' },
	{ "normalmap", required_argument, NULL, 'n' },
	{ "burstrod", no_argument, NULL, 'b' },
	{ "thrust", required_argument, NULL, 't' },
	{ "skybox", required_argument, NULL, 's' },
	{ "turret", required_argument, NULL, 'T' },
	{ "alphabynormal", no_argument, NULL, 'A' },
	{ "diffuse", required_argument, NULL, 'd' },
	{ "trap-nans", no_argument, NULL, 'f' },
	{ "replacement-assets", required_argument, NULL, 'r' },
	{ "warpgate", no_argument, NULL, 'w' },
	{ "no-skybox", no_argument, NULL, 'K' },
	{ 0, 0, 0, 0 },
};

static void process_options(int argc, char *argv[])
{
	int c;

	while (1) {
		int option_index;

		c = getopt_long(argc, argv, "IAB:T:bc:d:fC:KY:Z:e:hi:m:n:p:r:s:t:wW", long_options, &option_index);
		if (c < 0) {
			break;
		}
		switch (c) {
		case 'B':
			turret_mode = 1;
			turret_base_model = optarg;
			break;
		case 'K':
			no_skybox = 1;
			break;
		case 'T':
			turret_mode = 1;
			turret_model = optarg;
			break;
		case 'w':
			warpgate_mode = 1;
			break;
		case 'W':
			black_and_white = 1;
			break;
		case 'b':
			burst_rod_mode = 1;
			break;
		case 'p':
			planet_mode = 1;
			planetname = optarg;
			break;
		case 'A':
			use_alpha_by_normal = 1;
			break;
		case 'I':
			alpha_by_normal_invert = 1.0;
			break;
		case 'm':
			modelfile = optarg;
			break;
		case 'c':
			cubemap_mode = 1;
			cubemapname = optarg;
			break;
		case 'C':
			cylinder_albedo = optarg;
			cylindrical_axis = 0;
			break;
		case 'Y':
			cylinder_albedo = optarg;
			cylindrical_axis = 1;
			break;
		case 'Z':
			cylinder_albedo = optarg;
			cylindrical_axis = 2;
			break;
		case 'e':
			cylinder_emit = optarg;
			break;
		case 'i':
			process_int_option("icosahedron", optarg, &icosahedron_subdivision);
			if (icosahedron_subdivision > 4) {
				fprintf(stderr, "Max icosahedron subdivision is 4\n");
				icosahedron_subdivision = 4;
			} else if (icosahedron_subdivision < 0) {
				fprintf(stderr, "Min icosahedron subdivision is 0\n");
				icosahedron_subdivision = 0;
			}
			break;
		case 'n':
			normalmapname = optarg;
			break;
		case 'd':
			diffusename = optarg;
			break;
		case 'h':
			usage(program);
		case 't':
			thrust_mode = 1;
			thrustfile = optarg;
			break;
		case 's':
			skyboxfile = optarg;
			break;
		case 'f':
			trap_nans = 1;
			break;
		case 'r':
			replacement_assets_file = optarg;
			break;
		default:
			fprintf(stderr, "%s: Unknown option.\n", program);
			usage(program);
		}
	}
	check_modes();
	return;
}

static void figure_aspect_ratio(SDL_Window *window, int requested_x, int requested_y,
				int *x, int *y)
{
	SDL_GL_GetDrawableSize(window, &real_screen_width, &real_screen_height);
	*x = real_screen_width;
	*y = real_screen_height;
	screen_offset_x = 0;
	screen_offset_y = 0;

	int sw, sh, monitors;
	SDL_Rect bounds;

	monitors = SDL_GetNumVideoDisplays();
	if (monitors < 0)
		return;

	SDL_GetDisplayBounds(0, &bounds);
	sw = bounds.w;
	sh = bounds.h;
	screen_offset_x = bounds.x;
	screen_offset_y = bounds.y;


	if (requested_x <= 0 || requested_y <= 0) {
		*x = sw;
		*y = sh;
		return;
	}

	/*
	 * The requested long axis gets full screen dimension in that axis,
	 * and the other axis gets adjusted.  If that makes the other
	 * axis exceed screen dimensions, then further adjustments are made.
	 */
	if (requested_x > requested_y) {
		*x = sw;
		*y = (int) ((double) sw * (double) requested_y / (double) requested_x);
		if (*y > sh) {
			*y = sh;
			*x = (int) ((double) sh * (double) requested_x / (double) requested_y);
		}
	} else {
		*y = sh;
		*x = (int) ((double) sh * (double) requested_x / (double) requested_y);
		if (*x > sw) {
			*y = (int) ((double) sw * (double) requested_y / (double) requested_x);
			*x = sw;
		}
	}
}

static void enable_sdl_fullscreen_sanity(void)
{
	/* If SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS isn't set to zero,
	 * fullscreen window behavior is *insane* by default.
	 *
	 * Alt-tab and Alt-left-arrow and Alt-right-arrow will *minimize*
	 * the window, pushing it to the bottom of the stack, so when you
	 * alt-tab again, and expect the window to re-appear, it doesn't.
	 * Instead, a different window appears, and you have to alt-tab a
	 * zillion times through all your windows until you finally get to
	 * the bottom where your minimized fullscreen window sits, idiotically.
	 *
	 * Let's make sanity the default.  The last parameter of setenv()
	 * says do not overwrite the value if it is already set. This will
	 * allow for any completely insane individuals who somehow prefer
	 * this idiotc behavior to still have it.  But they will not get
	 * it by default.
	 */

	char *v = getenv("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS");
	if (v && strncmp(v, "1", 1) == 0) {
		fprintf(stderr, "Wow, you have SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS set to 1!\n");
		fprintf(stderr, "I highly recommend you set it to zero. But it's your sanity\n");
		fprintf(stderr, "at stake, not mine, so whatever. Let's proceed anyway.\n");
	}
	setenv("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0", 0);	/* Final 0 means don't override user's prefs */
								/* I am Very tempted to set it to 1. */
}

int main(int argc, char *argv[])
{
	char *filename;
	struct stat statbuf;
	int rc;

	setlocale(LC_ALL, "C");
	program = argc >= 0 ? argv[0] : "mesh_viewer";
	enable_sdl_fullscreen_sanity();

	if (argc < 2)
		usage(program);

	process_options(argc, argv);
#ifndef __APPLE__
	/* For some reason, with sdl2, feenenablexcept() is not declared. (sdl1 is fine) */
	if (trap_nans)
		feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
	if (replacement_assets_file) {
		rc = replacement_asset_read(replacement_assets_file, "share/snis", &replacement_asset);
		if (rc < 0) {
			fprintf(stderr, "Failed to read replacement assets file %s\n", replacement_assets_file);
			return -1;
		}
		stl_parser_set_asset_replacement_function(maybe_replace_asset);
	}

	filename = modelfile;
	if (!filename && !(planet_mode || burst_rod_mode || thrust_mode || turret_mode || warpgate_mode))
		usage(program);

	if (!planet_mode && !burst_rod_mode && !thrust_mode && !turret_mode && !warpgate_mode &&
			stat(filename, &statbuf) != 0) {
		fprintf(stderr, "%s: %s: %s\n", program, filename, strerror(errno));
		exit(1);
	}

	if (thrust_mode && !thrustfile)
		usage(program);

	/* First, initialize SDL's video subsystem. */
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		/* Failed, exit. */
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		quit(1);
	}

	screen = SDL_CreateWindow("Mesh Viewer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			3 * SCREEN_WIDTH / 4, 3 * SCREEN_HEIGHT / 4, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!screen) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		quit(1);
	}
	renderer = SDL_CreateRenderer(screen, -1, 0);

	figure_aspect_ratio(screen, -1, -1, &real_screen_width, &real_screen_height);
	SCREEN_WIDTH = real_screen_width;
	SCREEN_HEIGHT = real_screen_height;
	real_screen_width = (3 * real_screen_width) / 4;
	real_screen_height = (3 * real_screen_height) / 4;
	original_aspect_ratio = (float) real_screen_width / (float) real_screen_height;

	sng_setup_colors(NULL);

	snis_typefaces_init();
	sng_set_font_family(0);
	graph_dev_setup("share/snis/shader");
	setup_skybox("orange-haze");

	SDL_SetWindowSize(screen, real_screen_width, real_screen_height);
	window_manager_can_constrain_aspect_ratio =
		(constrain_aspect_ratio_via_xlib(screen, SCREEN_WIDTH, SCREEN_HEIGHT) == 0);
	sng_set_extent_size(SCREEN_WIDTH, SCREEN_HEIGHT);
	sng_set_screen_size(real_screen_width, real_screen_height);
	sng_set_clip_window(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	if (burst_rod_mode) {
		target_mesh = init_burst_rod_mesh(1000, LASER_VELOCITY * 2 / 3, 0.5, 0.5);
		atmosphere_mesh = NULL;
		material_init_textured_particle(&green_phaser_material);
		green_phaser_material.textured_particle.texture_id =
			graph_dev_load_texture(maybe_replace_asset("share/snis/textures/green-burst.png"), 0);
		green_phaser_material.textured_particle.radius = 0.75;
		green_phaser_material.textured_particle.time_base = 0.25;
	} else if (planet_mode) {
		target_mesh = mesh_unit_spherified_cube(64);
		atmosphere_mesh = mesh_unit_icosphere(icosahedron_subdivision);
		material_init_textured_planet(&planet_material);
		planet_material.textured_planet.texture_id = load_cubemap_textures(0, 0, planetname);
		if (normalmapname)
			planet_material.textured_planet.normalmap_id = load_cubemap_textures(0, 1, normalmapname);
		else
			planet_material.textured_planet.normalmap_id = 0;
		planet_material.textured_planet.ring_material = 0;
		material_init_atmosphere(&atmosphere_material);
		atmosphere_material.atmosphere.brightness = &atmosphere_brightness;
		atmosphere_material.atmosphere.brightness_modifier = 1.0;
	} else if (cubemap_mode) {
		target_mesh = snis_read_model(filename);
		atmosphere_mesh = NULL;
		material_init_textured_planet(&planet_material);
		planet_material.textured_planet.texture_id = load_cubemap_textures(0, 0,
					replacement_asset_lookup(cubemapname, &replacement_asset));
		planet_material.textured_planet.ring_material = 0;
		if (normalmapname)
			planet_material.textured_planet.normalmap_id = load_cubemap_textures(0, 1, normalmapname);
		else
			planet_material.textured_planet.normalmap_id = 0;
	} else if (thrust_mode) {
		target_mesh = init_thrust_mesh(70, 200, 1.3);
		material_init_textured_particle(&thrust_material);
		thrust_material.textured_particle.texture_id =
			graph_dev_load_texture(maybe_replace_asset(thrustfile), 0);
		thrust_material.textured_particle.radius = 1.5;
		thrust_material.textured_particle.time_base = 0.1;
	} else if (turret_mode) {
		target_mesh = snis_read_model(turret_model);
		turret_base_mesh = snis_read_model(turret_base_model);
		atmosphere_mesh = NULL;
	} else if (warpgate_mode) {
		target_mesh = mesh_fabricate_disc(1.0, 32);
		material_init_warp_gate_effect(&warpgate_effect_material);
		warpgate_effect_material.texture_mapped_unlit.texture_id =
			graph_dev_load_texture(maybe_replace_asset("share/snis/textures/warp-tunnel.png"), 0);
		warpgate_effect_material.texture_mapped_unlit.do_cullface = 0;
		warpgate_effect_material.texture_mapped_unlit.do_blend = 1;
		warpgate_effect_material.texture_mapped_unlit.alpha = 1.0;
	} else { /* just ordinary model mode */
		target_mesh = snis_read_model(filename);
		if (cylinder_albedo) {
			if (cylindrical_axis == 0)
				mesh_cylindrical_yz_uv_map(target_mesh);
			else if (cylindrical_axis == 1)
				mesh_cylindrical_xz_uv_map(target_mesh);
			else
				mesh_cylindrical_xy_uv_map(target_mesh);
			mesh_set_mikktspace_tangents_and_bitangents(target_mesh);
			material_init_texture_mapped(&cyl_albedo);
			cyl_albedo.texture_mapped.texture_id =
				graph_dev_load_texture(maybe_replace_asset(cylinder_albedo), 0);
			if (normalmapname && cyl_albedo.texture_mapped.normalmap_id <= 0)
				cyl_albedo.texture_mapped.normalmap_id =
					graph_dev_load_texture(maybe_replace_asset(normalmapname), 1);
		}
		if (cylinder_emit && cylinder_albedo) {
			if (cylindrical_axis == 0)
				mesh_cylindrical_yz_uv_map(target_mesh);
			else if (cylindrical_axis == 1)
				mesh_cylindrical_xz_uv_map(target_mesh);
			else
				mesh_cylindrical_xy_uv_map(target_mesh);
			mesh_set_mikktspace_tangents_and_bitangents(target_mesh);
			cyl_albedo.texture_mapped.emit_texture_id =
				graph_dev_load_texture(maybe_replace_asset(cylinder_emit), 0);
			if (normalmapname && cyl_albedo.texture_mapped.normalmap_id <= 0)
				cyl_albedo.texture_mapped.normalmap_id =
					graph_dev_load_texture(maybe_replace_asset(normalmapname), 1);
		}
		if (use_alpha_by_normal) {
			material_init_alpha_by_normal(&alpha_by_normal);
			alpha_by_normal.alpha_by_normal.alpha = 0.5;
			alpha_by_normal.alpha_by_normal.tint.red = 0.5;
			alpha_by_normal.alpha_by_normal.tint.green = 0.8;
			alpha_by_normal.alpha_by_normal.tint.blue = 1.0;
			alpha_by_normal.alpha_by_normal.invert = alpha_by_normal_invert;
		}
		if (diffusename) {
			material_init_texture_mapped(&diffuse_material);
			diffuse_material.texture_mapped.texture_id =
				graph_dev_load_texture(maybe_replace_asset(diffusename), 0);
			if (normalmapname)
				diffuse_material.texture_mapped.normalmap_id =
					graph_dev_load_texture(maybe_replace_asset(normalmapname), 1);
		}
		atmosphere_mesh = NULL;
	}
	if (!target_mesh) {
		printf("unable to read model file '%s'\n", filename);
		exit(-1);
	}

	light_mesh = mesh_fabricate_billboard(10, 10);

	struct material light_material;
	material_init_texture_mapped_unlit(&light_material);
	light_material.billboard_type = MATERIAL_BILLBOARD_TYPE_SCREEN;
	light_material.texture_mapped_unlit.texture_id =
			graph_dev_load_texture(maybe_replace_asset("share/snis/textures/sun.png"), 0);
	light_material.texture_mapped_unlit.do_blend = 1;

	light_mesh->material = &light_material;

	const double maxTimeBehind = 0.5;
	double delta = 1.0/(double)FPS;

	unsigned long frame = 0;
	double currentTime = time_now_double();
	double nextTime = currentTime + delta;
	while (1) {
		currentTime = time_now_double();

		if (currentTime - nextTime > maxTimeBehind)
			nextTime = currentTime;

		if (currentTime >= nextTime) {
			nextTime += delta;

			/* Process incoming events. */
			process_events();
			do_autospin();
			/* Draw the screen. */
			draw_screen();

			if (frame % FPS == 0) {
				graph_dev_reload_changed_textures();
				graph_dev_reload_changed_cubemap_textures();
			}
			if (reload_shaders) {
				graph_dev_reload_all_shaders();
				reload_shaders = 0;
			}
			frame++;
		} else {
			double timeToSleep = nextTime-currentTime;
			if (timeToSleep > 0)
				sleep_double(timeToSleep);
		}
		maybe_resize_window(screen);
	}

	/* Never reached. */
	return 0;
}
