
//============================================================
//
//  video.c - IOS video handling
//
//============================================================

// MAME headers
#include "emu.h"
#include "render.h"
#include "rendlay.h"
#include "ui/uimain.h"

// IOS headers
#include "iososd.h"

#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a)<(b) ? (b) : (a))

//============================================================
//  video_init
//============================================================

void ios_osd_interface::video_init()
{
    osd_printf_verbose("ios_osd_interface::video_init\n");

    // create our *single* render target, we dont do multiple windows or monitors
    m_target = machine().render().target_alloc();
    
    m_video_none = strcmp(options().value(OPTION_VIDEO), "none") == 0;

    m_min_width = 0;
    m_min_height = 0;
    m_vis_width = 0;
    m_vis_height = 0;
}

//============================================================
//  video_exit
//============================================================

void ios_osd_interface::video_exit()
{
    osd_printf_verbose("ios_osd_interface::video_exit\n");
    
    // free the render target
    machine().render().target_free(m_target);
    m_target = nullptr;
    
    if (m_callbacks.video_exit != NULL)
        m_callbacks.video_exit();
}

//============================================================
//  convert render_primitive to myosd_render_primitive
//============================================================
static void convert_prim(myosd_render_primitive &myosd_prim, const render_primitive &prim)
{
    myosd_prim.type = (prim.type-1);
    _Static_assert(MYOSD_RENDER_PRIMITIVE_LINE == render_primitive::primitive_type::LINE-1, "");
    _Static_assert(MYOSD_RENDER_PRIMITIVE_QUAD == render_primitive::primitive_type::QUAD-1, "");

    myosd_prim.bounds_x0 = prim.bounds.x0;
    myosd_prim.bounds_y0 = prim.bounds.y0;
    myosd_prim.bounds_x1 = prim.bounds.x1;
    myosd_prim.bounds_y1 = prim.bounds.y1;
    myosd_prim.color_a = prim.color.a;
    myosd_prim.color_r = prim.color.r;
    myosd_prim.color_g = prim.color.g;
    myosd_prim.color_b = prim.color.b;

    static int map_fmt[] = {MYOSD_TEXFORMAT_UNDEFINED, MYOSD_TEXFORMAT_PALETTE16, MYOSD_TEXFORMAT_RGB32, MYOSD_TEXFORMAT_ARGB32, MYOSD_TEXFORMAT_YUY16};
    _Static_assert(TEXFORMAT_UNDEFINED == 0, "");
    _Static_assert(TEXFORMAT_PALETTE16 == 1, "");
    _Static_assert(TEXFORMAT_RGB32     == 2, "");
    _Static_assert(TEXFORMAT_ARGB32    == 3, "");
    _Static_assert(TEXFORMAT_YUY16     == 4, "");
    myosd_prim.texformat = map_fmt[PRIMFLAG_GET_TEXFORMAT(prim.flags)];
    
    myosd_prim.texorient = PRIMFLAG_GET_TEXORIENT(prim.flags);
    _Static_assert(MYOSD_ORIENTATION_FLIP_X == ORIENTATION_FLIP_X, "");
    _Static_assert(MYOSD_ORIENTATION_FLIP_Y == ORIENTATION_FLIP_Y, "");
    _Static_assert(MYOSD_ORIENTATION_SWAP_XY == ORIENTATION_SWAP_XY, "");

    myosd_prim.blendmode = PRIMFLAG_GET_BLENDMODE(prim.flags);
    _Static_assert(MYOSD_BLENDMODE_NONE == +BLENDMODE_NONE, "");
    _Static_assert(MYOSD_BLENDMODE_ALPHA == +BLENDMODE_ALPHA, "");
    _Static_assert(MYOSD_BLENDMODE_RGB_MULTIPLY == +BLENDMODE_RGB_MULTIPLY, "");
    _Static_assert(MYOSD_BLENDMODE_ADD == +BLENDMODE_ADD, "");

    myosd_prim.antialias = PRIMFLAG_GET_ANTIALIAS(prim.flags);
    myosd_prim.screentex = PRIMFLAG_GET_SCREENTEX(prim.flags);
    myosd_prim.texwrap   = PRIMFLAG_GET_TEXWRAP(prim.flags);
    myosd_prim.unused    = 0;
    
    // TODO: what are these?
    PRIMFLAG_GET_TEXSHADE(prim.flags);
    PRIMFLAG_GET_VECTOR(prim.flags);
    PRIMFLAG_GET_VECTORBUF(prim.flags);
    
    myosd_prim.width = prim.width;
    myosd_prim.texture_base = prim.texture.base;
    myosd_prim.texture_rowpixels = prim.texture.rowpixels;
    myosd_prim.texture_width = prim.texture.width;
    myosd_prim.texture_height = prim.texture.height;
    myosd_prim.texture_palette = prim.texture.palette;
    myosd_prim.texture_seqid = prim.texture.seqid;
    
    // TODO: what is unique_id?? how is it different from seqid?
    prim.texture.unique_id;
    
    myosd_prim.texcoords[0].u = prim.texcoords.tl.u;
    myosd_prim.texcoords[0].v = prim.texcoords.tl.v;
    myosd_prim.texcoords[1].u = prim.texcoords.tr.u;
    myosd_prim.texcoords[1].v = prim.texcoords.tr.v;
    myosd_prim.texcoords[2].u = prim.texcoords.bl.u;
    myosd_prim.texcoords[2].v = prim.texcoords.bl.v;
    myosd_prim.texcoords[3].u = prim.texcoords.br.u;
    myosd_prim.texcoords[3].v = prim.texcoords.br.v;
}

//============================================================
//  update
//============================================================

void ios_osd_interface::update(bool skip_redraw)
{
    osd_printf_verbose("ios_osd_interface::update\n");

    // if skipping this redraw, bail
    if (skip_redraw || m_callbacks.video_draw == NULL || m_video_none)
        return;
    
    int vis_width, vis_height;
    int min_width, min_height;
    target()->compute_minimum_size(min_width, min_height);
    target()->compute_visible_area(MAX(640,myosd_display_width), MAX(480,myosd_display_height), 1.0, target()->orientation(), vis_width, vis_height);
     
    // check for a change in the min-size of render target *or* size of the vis screen
    if (min_width != m_min_width || min_height != m_min_height ||
        vis_width != m_vis_width || vis_height != m_vis_height) {
        
        m_min_width = min_width;
        m_min_height = min_height;
        m_vis_width = vis_width;
        m_vis_height = vis_height;
        
        if (m_callbacks.video_init != NULL)
            m_callbacks.video_init(vis_width, vis_height, min_width, min_height);
    }

    target()->set_bounds(vis_width, vis_height, 1.0);
    render_primitive_list *primlist = &target()->get_primitives();
    
    primlist->acquire_lock();

    // TODO: is 4K enough? make dynamic?
    static myosd_render_primitive myosd_prim[4096];
    int i = 0;
    
    // convert from render_primitive(s) to myosd_render_primitive(s)
    for (render_primitive &prim : *primlist)
    {
        if (i == sizeof(myosd_prim)/sizeof(myosd_prim[0]))
            break;

        convert_prim(myosd_prim[i], prim);
        myosd_prim[i].next = &myosd_prim[i+1];
        i++;
    }
    myosd_prim[i-1].next = NULL;
    
    m_callbacks.video_draw(myosd_prim, vis_width, vis_height);

    primlist->release_lock();
}


