/*****************************************************************************
 * avcodec.c: VDPAU decoder for libav
 *****************************************************************************
 * Copyright (C) 2012-2013 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/vdpau.h>
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_fourcc.h>
#include <vlc_picture.h>
#include <vlc_codec.h>
#include <vlc_xlib.h>
#include "vlc_vdpau.h"
#include "../../codec/avcodec/va.h"

struct video_context_private
{
    vdp_t *vdp;
    vlc_vdp_video_field_t *pool[];
};

struct vlc_va_sys_t
{
    VdpDevice device;
    VdpChromaType type;
    void *hwaccel_context;
    uint32_t width;
    uint32_t height;
    vlc_video_context *vctx;
};

static inline struct video_context_private *GetVDPAUContextPrivate(vlc_video_context *vctx)
{
    return (struct video_context_private *)
        vlc_video_context_GetPrivate( vctx, VLC_VIDEO_CONTEXT_VDPAU );
}

static vlc_vdp_video_field_t *CreateSurface(vlc_va_t *va)
{
    vlc_va_sys_t *sys = va->sys;
    struct video_context_private *vctx_priv = GetVDPAUContextPrivate(sys->vctx);
    VdpVideoSurface surface;
    VdpStatus err;

    err = vdp_video_surface_create(vctx_priv->vdp, sys->device, sys->type,
                                   sys->width, sys->height, &surface);
    if (err != VDP_STATUS_OK)
    {
        msg_Err(va, "%s creation failure: %s", "video surface",
                vdp_get_error_string(vctx_priv->vdp, err));
        return NULL;
    }

    vlc_vdp_video_field_t *field = vlc_vdp_video_create(vctx_priv->vdp, surface);
    if (unlikely(field == NULL))
        vdp_video_surface_destroy(vctx_priv->vdp, surface);
    return field;
}

static vlc_vdp_video_field_t *GetSurface(vlc_va_sys_t *sys)
{
    vlc_vdp_video_field_t *f;
    struct video_context_private *vctx_priv = GetVDPAUContextPrivate(sys->vctx);

    for (unsigned i = 0; (f = vctx_priv->pool[i]) != NULL; i++)
    {
        uintptr_t expected = 1;

        if (atomic_compare_exchange_strong(&f->frame->refs, &expected, 2))
        {
            vlc_vdp_video_field_t *field = vlc_vdp_video_copy(f);
            atomic_fetch_sub(&f->frame->refs, 1);
            return field;
        }
    }
    return NULL;
}

static vlc_vdp_video_field_t *Get(vlc_va_sys_t *sys)
{
    vlc_vdp_video_field_t *field;
    unsigned tries = (VLC_TICK_FROM_SEC(1) + VOUT_OUTMEM_SLEEP) / VOUT_OUTMEM_SLEEP;

    while ((field = GetSurface(sys)) == NULL)
    {
        if (--tries == 0)
            return NULL;
        /* Pool empty. Wait for some time as in src/input/decoder.c.
         * XXX: Both this and the core should use a semaphore or a CV. */
        vlc_tick_sleep(VOUT_OUTMEM_SLEEP);
    }

    return field;
}

static int Lock(vlc_va_t *va, picture_t *pic, uint8_t **data)
{
    vlc_va_sys_t *sys = va->sys;
    vlc_vdp_video_field_t *field = Get(sys);
    if (field == NULL)
        return VLC_ENOMEM;

    pic->context = &field->context;
    *data = (void *)(uintptr_t)field->frame->surface;
    return VLC_SUCCESS;
}

static void Close(vlc_va_t *va)
{
    vlc_va_sys_t *sys = va->sys;

    vlc_video_context_Release(sys->vctx);
    if (sys->hwaccel_context)
        av_free(sys->hwaccel_context);
    free(sys);
}

static const struct vlc_va_operations ops = { Lock, Close, };

static void DestroyVDPAUVideoContext(void *private)
{
    struct video_context_private *vctx_priv = private;
    for (unsigned i = 0; vctx_priv->pool[i] != NULL; i++)
        vlc_vdp_video_destroy(vctx_priv->pool[i]);
    vdp_release_x11(vctx_priv->vdp);
}

const struct vlc_video_context_operations vdpau_vctx_ops = {
    DestroyVDPAUVideoContext,
};

static int Open(vlc_va_t *va, AVCodecContext *avctx, const AVPixFmtDescriptor *desc,
                enum PixelFormat pix_fmt,
                const es_format_t *fmt, vlc_decoder_device *dec_device,
                vlc_video_context **vtcx_out)
{
    if (pix_fmt != AV_PIX_FMT_VDPAU|| dec_device == NULL ||
        GetVDPAUOpaqueDevice(dec_device) == NULL)
        return VLC_EGENERIC;

    (void) fmt;
    (void) desc;
    void *func;
    VdpStatus err;
    VdpChromaType type;
    uint32_t width, height;

    if (av_vdpau_get_surface_parameters(avctx, &type, &width, &height))
        return VLC_EGENERIC;

    switch (type)
    {
        case VDP_CHROMA_TYPE_420:
        case VDP_CHROMA_TYPE_422:
        case VDP_CHROMA_TYPE_444:
            break;
        default:
            msg_Err(va, "unsupported chroma type %"PRIu32, type);
            return VLC_EGENERIC;
    }

    if (!vlc_xlib_init(VLC_OBJECT(va)))
    {
        msg_Err(va, "Xlib is required for VDPAU");
        return VLC_EGENERIC;
    }

    unsigned refs = avctx->refs + 2 * avctx->thread_count + 5;
    vlc_va_sys_t *sys = malloc(sizeof (*sys));
    if (unlikely(sys == NULL))
       return VLC_ENOMEM;

    sys->vctx = vlc_video_context_Create( dec_device, VLC_VIDEO_CONTEXT_VDPAU,
                                          sizeof(struct video_context_private) +
                                           (refs + 1) * sizeof (vlc_vdp_video_field_t),
                                          &vdpau_vctx_ops );
    if (sys->vctx == NULL)
    {
        free(sys);
        return VLC_ENOMEM;
    }

    struct video_context_private *vctx_priv = GetVDPAUContextPrivate(sys->vctx);

    sys->type = type;
    sys->width = width;
    sys->height = height;
    sys->hwaccel_context = NULL;
    vctx_priv->vdp = GetVDPAUOpaqueDevice(dec_device);
    vdp_hold_x11(vctx_priv->vdp, &sys->device);

    unsigned flags = AV_HWACCEL_FLAG_ALLOW_HIGH_DEPTH;

    err = vdp_get_proc_address(vctx_priv->vdp, sys->device,
                               VDP_FUNC_ID_GET_PROC_ADDRESS, &func);
    if (err != VDP_STATUS_OK)
        goto error;

    if (av_vdpau_bind_context(avctx, sys->device, func, flags))
        goto error;
    sys->hwaccel_context = avctx->hwaccel_context;
    va->sys = sys;

    unsigned i = 0;
    while (i < refs)
    {
        vctx_priv->pool[i] = CreateSurface(va);
        if (vctx_priv->pool[i] == NULL)
            break;
        i++;
    }
    vctx_priv->pool[i] = NULL;

    if (i < avctx->refs + 3u)
    {
        msg_Err(va, "not enough video RAM");
        while (i > 0)
            vlc_vdp_video_destroy(vctx_priv->pool[--i]);
        goto error;
    }

    if (i < refs)
        msg_Warn(va, "video RAM low (allocated %u of %u buffers)",
                 i, refs);

    const char *infos;
    if (vdp_get_information_string(vctx_priv->vdp, &infos) == VDP_STATUS_OK)
        msg_Info(va, "Using %s", infos);

    *vtcx_out = sys->vctx;
    va->ops = &ops;
    return VLC_SUCCESS;

error:
    if (sys->vctx)
        vlc_video_context_Release(sys->vctx);
    if (sys->hwaccel_context)
        av_free(sys->hwaccel_context);
    free(sys);
    return VLC_EGENERIC;
}

vlc_module_begin()
    set_description(N_("VDPAU video decoder"))
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_VCODEC)
    set_va_callback(Open, 100)
    add_shortcut("vdpau")
vlc_module_end()
