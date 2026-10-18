#include "zgl.h"
#if defined(BTRON_PS2_TARGET)
#include "../backend_eegs.h"
#include "../gl_dispatch.h"
#endif

void glopClearColor(GLParam* p) {
	GLContext* c = gl_get_context();
	c->clear_color.v[0] = p[1].f;
	c->clear_color.v[1] = p[2].f;
	c->clear_color.v[2] = p[3].f;
	c->clear_color.v[3] = p[4].f;
}
void glopClearDepth(GLParam* p) {
	GLContext* c = gl_get_context();
	c->clear_depth = p[1].f;
}

void glopClear(GLParam* p) {
	GLContext* c = gl_get_context();
	GLint mask = p[1].i;
	GLint z = 0;
	GLint r = (GLint)(c->clear_color.v[0] * COLOR_MULT_MASK);
	GLint g = (GLint)(c->clear_color.v[1] * COLOR_MULT_MASK);
	GLint b = (GLint)(c->clear_color.v[2] * COLOR_MULT_MASK);

	/* TODO : correct value of Z */

	ZB_clear(c->zb, mask & GL_DEPTH_BUFFER_BIT, z, mask & GL_COLOR_BUFFER_BIT, r, g, b);

#if defined(BTRON_PS2_TARGET)
	if (gl_active_backend() == GL_BACKEND_EEGS && (mask & GL_COLOR_BUFFER_BIT)) {
		const uint32_t cr = (uint32_t)(c->clear_color.v[0] * 255.0f);
		const uint32_t cg = (uint32_t)(c->clear_color.v[1] * 255.0f);
		const uint32_t cb = (uint32_t)(c->clear_color.v[2] * 255.0f);
		const uint32_t ca = (uint32_t)(c->clear_color.v[3] * 255.0f);
		const uint32_t rgba = (cr & 0xFFu) | ((cg & 0xFFu) << 8) | ((cb & 0xFFu) << 16) | ((ca & 0xFFu) << 24);
		int32_t w = c->zb ? c->zb->xsize : (int32_t)c->viewport.xsize;
		int32_t h = c->zb ? c->zb->ysize : (int32_t)c->viewport.ysize;
		if (w > 0 && h > 0) {
			eegs_clear(0, 0, w, h, rgba);
		}
	}
#endif
}
