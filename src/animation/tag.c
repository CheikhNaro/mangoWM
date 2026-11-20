#include "mango/animation/tag.h"
#include "mango/animation/client.h"
#include "mango/common/server.h"
#include "mango/common/util.h"
#include "mango/layout/layout.h"
#include "mango/manage/client.h"
#include "mango/manage/monitor.h"
#include "mango/manage/tab.h"
#include <stdio.h>

void set_tagin_animation(Monitor *m, Client *c) {
	if (c->animation.running) {
		c->animainit_geom.x = c->animation.current.x;
		c->animainit_geom.y = c->animation.current.y;
		return;
	}

	if ((c->isglobal || c->isunglobal) ||
		(c->tags & (1 << (m->pertag->prevtag - 1)) &&
		 c->tags & (1 << (m->pertag->curtag - 1)))) {
		c->animation.tagouting = false;
		c->animation.tagouted = false;
		c->animation.tagining = false;
		c->animation.action = MOVE;
		return;
	}

	bool going_forward = m->carousel_anim_dir
							 ? m->carousel_anim_dir > 0
							 : m->pertag->curtag > m->pertag->prevtag;

	if (going_forward) {

		c->animainit_geom.x = config.tag_animation_direction == VERTICAL
								  ? c->animation.current.x
								  : MANGO_MAX(c->mon->m.x + c->mon->m.width,
											  c->geom.x + c->mon->m.width);
		c->animainit_geom.y = config.tag_animation_direction == VERTICAL
								  ? MANGO_MAX(c->mon->m.y + c->mon->m.height,
											  c->geom.y + c->mon->m.height)
								  : c->animation.current.y;

	} else {

		c->animainit_geom.x = config.tag_animation_direction == VERTICAL
								  ? c->animation.current.x
								  : MANGO_MIN(m->m.x - c->geom.width,
											  c->geom.x - c->mon->m.width);
		c->animainit_geom.y = config.tag_animation_direction == VERTICAL
								  ? MANGO_MIN(m->m.y - c->geom.height,
											  c->geom.y - c->mon->m.height)
								  : c->animation.current.y;
	}
}

void set_arrange_visible(Monitor *m, Client *c, bool want_animation) {
	c->tag_visible = true;

	bool was_enabled = c->scene->node.enabled;
	bool in_place = was_enabled && !c->animation.running &&
					wlr_box_equal(&c->animation.current, &c->geom);
	bool tag_switch = !in_place && !c->animation.tag_from_rule &&
					  want_animation && m->pertag->prevtag != 0 &&
					  m->pertag->curtag != 0 && client_animations_enabled(c);

	if (!ISTILED(c) || (!c->is_clip_to_hide || !is_scroller_layout(c->mon))) {
		c->is_clip_to_hide = false;
		client_update_visibility(c);
	}

	/* Scratchpad clients animate in when shown
	 * (scratchpad_animation_type: none/fade/slide/zoom). */
	if (SCRATCHPAD_SHOWN(c) && c->scratchpad_tagin) {
		c->scratchpad_tagin = false;
		c->animation.tag_from_rule = false;
		c->animation.tagouted = false;
		int32_t sp_type = config.scratchpad_animation_type;
		bool fade = sp_type == ANIM_TYPE_FADE;
		if (!fade && c->blur)
			/* Reset blur alpha in case a previous fade left it at 0. */
			wlr_scene_blur_set_alpha(c->blur, 1.0f);
		/* Reverse an in-flight hide instead of restarting from the top. */
		bool reversing =
			c->animation.tagouting && c->animation.running &&
			(fade ||
			 !wlr_box_equal(&c->animation.current, &c->animation.initial));
		c->animation.tagouting = false;
		if (sp_type == ANIM_TYPE_NONE || !client_animations_enabled(c)) {
			/* No animation: show instantly. */
			c->animation.running = false;
			c->animation.tagining = false;
			c->scratchpad_animation_opacity = 1.0f;
			if (c->blur)
				wlr_scene_blur_set_alpha(c->blur, 1.0f);
			c->animainit_geom = c->geom;
			resize(c, c->geom, (ResizeOpts){.skip_ov_enter_anim = true});
			return;
		}
		c->animation.tagining = true;
		c->animainit_geom = c->geom;
		if (reversing) {
			c->animainit_geom.x = c->animation.current.x;
			c->animainit_geom.y = c->animation.current.y;
			if (sp_type == ANIM_TYPE_ZOOM)
				/* Resume the interrupted zoom from its current size. */
				c->animainit_geom = c->animation.current;
			if (fade)
				c->scratchpad_animation_start_opacity =
					c->scratchpad_animation_opacity;
		} else if (fade) {
			c->scratchpad_animation_opacity = 0.0f;
			c->scratchpad_animation_start_opacity = 0.0f;
		} else if (sp_type == ANIM_TYPE_ZOOM) {
			/* Zoom in from a centered, smaller box. */
			c->animainit_geom.width =
				c->geom.width * config.zoom_initial_ratio;
			c->animainit_geom.height =
				c->geom.height * config.zoom_initial_ratio;
			c->animainit_geom.x =
				c->geom.x + (c->geom.width - c->animainit_geom.width) / 2;
			c->animainit_geom.y =
				c->geom.y + (c->geom.height - c->animainit_geom.height) / 2;
		} else {
			c->animainit_geom.y = c->mon->m.y - c->geom.height;
		}
		resize(c, c->geom, (ResizeOpts){.skip_ov_enter_anim = true});
		return;
	}

	/* Special workspace clients slide in from above the monitor */
	/* Special workspace windows animate in (scratchpad_animation_type:
	 * none/fade/slide/zoom). Newly opened clients keep their standard open
	 * animation instead. */
	if ((c->tags & TAG0_MASK) && !c->is_pending_open_animation) {
		c->animation.tag_from_rule = false;
		c->animation.tagouted = false;
		int32_t sp_type = config.scratchpad_animation_type;
		bool fade = sp_type == ANIM_TYPE_FADE;
		if (!fade && c->blur)
			/* Reset blur alpha in case a previous fade left it at 0. */
			wlr_scene_blur_set_alpha(c->blur, 1.0f);
		/* Reverse an in-flight hide instead of restarting from the top. */
		bool reversing =
			c->animation.tagouting && c->animation.running &&
			(fade ||
			 !wlr_box_equal(&c->animation.current, &c->animation.initial));
		c->animation.tagouting = false;
		if (sp_type == ANIM_TYPE_NONE || !want_animation ||
			!client_animations_enabled(c)) {
			/* No animation: show instantly. */
			c->animation.running = false;
			c->animation.tagining = false;
			c->scratchpad_animation_opacity = 1.0f;
			if (c->blur)
				wlr_scene_blur_set_alpha(c->blur, 1.0f);
			c->animainit_geom = c->geom;
			resize(c, c->geom, (ResizeOpts){.skip_ov_enter_anim = true});
			return;
		}
		c->animation.tagining = true;
		c->animainit_geom = c->geom;
		{
			FILE *dbg = fopen("/tmp/mango_sp_zoom.log", "a");
			if (dbg) {
				fprintf(dbg,
						"win show START now=%lld type=%d reversing=%d "
						"was_running=%d tagouting=%d\n",
						(long long)get_now_in_ms(), sp_type, reversing,
						c->animation.running, c->animation.tagouting);
				fclose(dbg);
			}
		}
		if (reversing) {
			c->animainit_geom.x = c->animation.current.x;
			c->animainit_geom.y = c->animation.current.y;
			if (sp_type == ANIM_TYPE_ZOOM)
				/* Resume the interrupted zoom from its current size. */
				c->animainit_geom = c->animation.current;
			if (fade)
				c->scratchpad_animation_start_opacity =
					c->scratchpad_animation_opacity;
		} else if (fade) {
			c->scratchpad_animation_opacity = 0.0f;
			c->scratchpad_animation_start_opacity = 0.0f;
		} else if (sp_type == ANIM_TYPE_ZOOM) {
			/* Zoom in from a centered, smaller box. */
			c->animainit_geom.width =
				c->geom.width * config.zoom_initial_ratio;
			c->animainit_geom.height =
				c->geom.height * config.zoom_initial_ratio;
			c->animainit_geom.x =
				c->geom.x + (c->geom.width - c->animainit_geom.width) / 2;
			c->animainit_geom.y =
				c->geom.y + (c->geom.height - c->animainit_geom.height) / 2;
		} else {
			c->animainit_geom.y = c->mon->m.y - c->geom.height;
		}
		resize(c, c->geom, (ResizeOpts){.skip_ov_enter_anim = true});
		return;
	}

	if (tag_switch) {
		c->animation.tagining = true;
		set_tagin_animation(m, c);
	} else {
		c->animainit_geom.x = c->animation.current.x;
		c->animainit_geom.y = c->animation.current.y;
	}

	c->animation.tag_from_rule = false;
	c->animation.tagouting = false;
	c->animation.tagouted = false;
	resize(c, c->geom, (ResizeOpts){.skip_ov_enter_anim = true});
}

void set_tagout_animation(Monitor *m, Client *c) {
	if ((c->isglobal || c->isunglobal) ||
		(c->tags & (1 << (m->pertag->prevtag - 1)) &&
		 c->tags & (1 << (m->pertag->curtag - 1)))) {
		c->animation.tagouting = false;
		c->animation.tagouted = false;
		c->animation.tagining = false;
		c->animation.action = MOVE;
		return;
	}

	bool going_forward = m->carousel_anim_dir
							 ? m->carousel_anim_dir > 0
							 : m->pertag->curtag > m->pertag->prevtag;
	if (going_forward) {
		c->pending = c->geom;
		c->pending.x = config.tag_animation_direction == VERTICAL
						   ? c->animation.current.x
						   : MANGO_MIN(c->mon->m.x - c->geom.width,
									   c->geom.x - c->mon->m.width);
		c->pending.y = config.tag_animation_direction == VERTICAL
						   ? MANGO_MIN(c->mon->m.y - c->geom.height,
									   c->geom.y - c->mon->m.height)
						   : c->animation.current.y;

		resize(c, c->geom, (ResizeOpts){.interact = 0});
	} else {
		c->pending = c->geom;
		c->pending.x = config.tag_animation_direction == VERTICAL
						   ? c->animation.current.x
						   : MANGO_MAX(c->mon->m.x + c->mon->m.width,
									   c->geom.x + c->mon->m.width);
		c->pending.y = config.tag_animation_direction == VERTICAL
						   ? MANGO_MAX(c->mon->m.y + c->mon->m.height,
									   c->geom.y + c->mon->m.height)
						   : c->animation.current.y;
		resize(c, c->geom, (ResizeOpts){.interact = 0});
	}
}
void set_arrange_hidden(Monitor *m, Client *c, bool want_animation) {
	c->tag_visible = false;

	/* In overview every tag window must show its card and must not be disabled
	 * by the hiding logic. */
	if (c->ov_card_tree) {
		c->is_clip_to_hide = false;
		client_update_visibility(c);
		c->animation.running = false;
		c->animation.tagining = false;
		c->animation.tagouting = false;
		return;
	}

	/* Scratchpad windows animate out when hidden
	 * (scratchpad_animation_type: none/fade/slide/zoom). */
	if (!(c->tags & TAG0_MASK) && c->is_in_scratchpad && c->isminimized) {
		if (config.scratchpad_animation_type != ANIM_TYPE_NONE &&
			client_animations_enabled(c) && !c->animation.tagouted) {
			int32_t sp_type = config.scratchpad_animation_type;
			bool fade = sp_type == ANIM_TYPE_FADE;
			if (!fade && c->blur)
				/* Reset blur alpha in case a previous fade left it at 0. */
				wlr_scene_blur_set_alpha(c->blur, 1.0f);
			bool reversing = c->animation.tagining && c->animation.running;
			c->animation.tagouting = true;
			c->animation.tagining = false;
			c->pending = c->geom;
			if (fade) {
				if (!reversing) {
					c->scratchpad_animation_opacity = 1.0f;
			if (c->blur)
				wlr_scene_blur_set_alpha(c->blur, 1.0f);
					c->scratchpad_animation_start_opacity = 1.0f;
				} else {
					c->scratchpad_animation_start_opacity =
						c->scratchpad_animation_opacity;
				}
			} else if (sp_type == ANIM_TYPE_ZOOM) {
				/* Zoom out to a centered, smaller box. */
				c->pending.width = c->geom.width * config.zoom_initial_ratio;
				c->pending.height =
					c->geom.height * config.zoom_initial_ratio;
				c->pending.x =
					c->geom.x + (c->geom.width - c->pending.width) / 2;
				c->pending.y =
					c->geom.y + (c->geom.height - c->pending.height) / 2;
			} else {
				c->pending.y = c->mon->m.y - c->geom.height;
			}
			resize(c, c->geom, (ResizeOpts){.interact = 0});
		} else {
			c->animation.running = false;
			c->animation.tagouting = false;
			c->animation.tagining = false;
			if (config.scratchpad_animation_type == ANIM_TYPE_FADE)
				c->scratchpad_animation_opacity = 0.0f;
			client_update_visibility(c);
			c->animainit_geom = c->current = c->pending = c->animation.current =
				c->geom;
		}
		return;
	}

	/* Special workspace windows animate out or hide when the special
	 * workspace is not active (scratchpad_animation_type:
	 * none/fade/slide/zoom). */
	if (c->tags & TAG0_MASK) {
		int32_t sp_type = config.scratchpad_animation_type;
		if (sp_type != ANIM_TYPE_NONE && want_animation && !m->isoverview &&
			client_animations_enabled(c) && !c->animation.tagouted) {
			{
				FILE *dbg = fopen("/tmp/mango_sp_zoom.log", "a");
				if (dbg) {
					fprintf(dbg,
							"win hide START now=%lld type=%d reversing=%d "
							"was_running=%d tagouted=%d\n",
							(long long)get_now_in_ms(), sp_type,
							c->animation.tagining && c->animation.running,
							c->animation.running, c->animation.tagouted);
					fclose(dbg);
				}
			}
			bool fade = sp_type == ANIM_TYPE_FADE;
			if (!fade && c->blur)
				/* Reset blur alpha in case a previous fade left it at 0. */
				wlr_scene_blur_set_alpha(c->blur, 1.0f);
			bool reversing = c->animation.tagining && c->animation.running;
			c->animation.tagouting = true;
			c->animation.tagining = false;
			c->pending = c->geom;
			if (fade) {
				if (!reversing) {
					c->scratchpad_animation_opacity = 1.0f;
			if (c->blur)
				wlr_scene_blur_set_alpha(c->blur, 1.0f);
					c->scratchpad_animation_start_opacity = 1.0f;
				} else {
					c->scratchpad_animation_start_opacity =
						c->scratchpad_animation_opacity;
				}
			} else if (sp_type == ANIM_TYPE_ZOOM) {
				/* Zoom out to a centered, smaller box. */
				c->pending.width = c->geom.width * config.zoom_initial_ratio;
				c->pending.height =
					c->geom.height * config.zoom_initial_ratio;
				c->pending.x =
					c->geom.x + (c->geom.width - c->pending.width) / 2;
				c->pending.y =
					c->geom.y + (c->geom.height - c->pending.height) / 2;
			} else {
				c->pending.y = c->mon->m.y - c->geom.height;
			}
			resize(c, c->geom, (ResizeOpts){.interact = 0});
		} else {
			c->animation.running = false;
			c->animation.tagouting = false;
			c->animation.tagining = false;
			if (sp_type == ANIM_TYPE_FADE)
				c->scratchpad_animation_opacity = 0.0f;
			client_update_visibility(c);
			c->animainit_geom = c->current = c->pending = c->animation.current =
				c->geom;
		}
		return;
	}

	if (want_animation && !c->is_tab_hidden && !c->animation.tagouted &&
		(c->tags & (1 << (m->pertag->prevtag - 1))) &&
		m->pertag->prevtag != 0 && m->pertag->curtag != 0 &&
		client_animations_enabled(c)) {
		c->animation.tagouting = true;
		c->animation.tagining = false;
		set_tagout_animation(m, c);
	} else {
		c->animation.running = false;
		c->animation.tagining = false;
		c->animation.tagouting = false;
		c->animation.tagouted = true;
		client_update_visibility(c);
		c->animainit_geom = c->current = c->pending = c->animation.current =
			c->geom;
	}
}
