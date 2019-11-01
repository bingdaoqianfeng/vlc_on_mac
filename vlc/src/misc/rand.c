/*****************************************************************************
 * rand.c : non-predictible random bytes generator
 *****************************************************************************
 * Copyright © 2007 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_rand.h>

static struct
{
    bool           init;
    unsigned short subi[3];
    vlc_mutex_t    lock;
} rand48 = { false, { 0, 0, 0, }, VLC_STATIC_MUTEX, };

static void init_rand48 (void)
{
    if (!rand48.init)
    {
        vlc_rand_bytes (rand48.subi, sizeof (rand48.subi));
        rand48.init = true;
#if 0 // short would be more than 16-bits ?
        for (unsigned i = 0; i < 3; i++)
            subi[i] &= 0xffff;
#endif
    }
}

/**
 * PRNG uniformly distributed between 0.0 and 1.0 with 48-bits precision.
 *
 * @note Contrary to POSIX drand48(), this function is thread-safe.
 * @warning Series generated by this function are not reproducible.
 * Use erand48() if you need reproducible series.
 *
 * @return a double value within [0.0, 1.0] inclusive
 */
double vlc_drand48 (void)
{
    double ret;

    vlc_mutex_lock (&rand48.lock);
    init_rand48 ();
    ret = erand48 (rand48.subi);
    vlc_mutex_unlock (&rand48.lock);
    return ret;
}

/**
 * PRNG uniformly distributed between 0 and 2^32 - 1.
 *
 * @note Contrary to POSIX lrand48(), this function is thread-safe.
 * @warning Series generated by this function are not reproducible.
 * Use nrand48() if you need reproducible series.
 *
 * @return an integral value within [0.0, 2^32-1] inclusive
 */
long vlc_lrand48 (void)
{
    long ret;

    vlc_mutex_lock (&rand48.lock);
    init_rand48 ();
    ret = nrand48 (rand48.subi);
    vlc_mutex_unlock (&rand48.lock);
    return ret;
}

/**
 * PRNG uniformly distributed between -2^32 and 2^32 - 1.
 *
 * @note Contrary to POSIX mrand48(), this function is thread-safe.
 * @warning Series generated by this function are not reproducible.
 * Use jrand48() if you need reproducible series.
 *
 * @return an integral value within [-2^32, 2^32-1] inclusive
 */
long vlc_mrand48 (void)
{
    long ret;

    vlc_mutex_lock (&rand48.lock);
    init_rand48 ();
    ret = jrand48 (rand48.subi);
    vlc_mutex_unlock (&rand48.lock);
    return ret;
}