/**
 * @file cosim_abi.h
 * @brief Binary-compatible copy of the ngspice d_cosim interface.
 *
 * Mirrors <ngspice/cosim.h> and the Digital_t type of <ngspice/cmtypes.h>
 * (ngspice >= 42, modified BSD license) so this project builds without the
 * ngspice development headers. Field order and enum values MUST stay
 * identical to the upstream definitions.
 *
 * SPDX-License-Identifier: BSD-3-Clause (interface copied from ngspice)
 */

#ifndef COSIM_ABI_H
#define COSIM_ABI_H

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- Digital_t, from ngspice/cmtypes.h ---------------------------------- */

typedef enum
{
    ZERO,
    ONE,
    UNKNOWN
} Digital_State_t;

typedef enum
{
    STRONG,
    RESISTIVE,
    HI_IMPEDANCE,
    UNDETERMINED
} Digital_Strength_t;

typedef struct
{
    Digital_State_t    state;
    Digital_Strength_t strength;
} Digital_t;

/* ---- Co-simulation interface, from ngspice/cosim.h ---------------------- */

typedef enum
{
    Normal,
    After_input,
    Both
} Cosim_method;

struct co_info
{
    /* Set by the co-simulator in Cosim_setup(). */
    unsigned int in_count;
    unsigned int out_count;
    unsigned int inout_count;

    /* Optional, called before the library is unloaded. */
    void (*cleanup)(struct co_info *);

    /* Advance to vtime; pause and lower vtime when an output changes. */
    void (*step)(struct co_info *pinfo);

    /* Called by ngspice for input (then inout) changes. */
    void (*in_fn)(struct co_info *, unsigned int, Digital_t *);

    /* Provided by ngspice: report output (then inout) changes. */
    void (*out_fn)(struct co_info *, unsigned int, Digital_t *);

    void           *handle;   /* Co-simulator private data. */
    volatile double vtime;    /* Co-simulation time in seconds. */
    Cosim_method    method;

    /* Strings from the lib_args / sim_args parameters of the .model card. */
    unsigned int              lib_argc;
    unsigned int              sim_argc;
    const char *const *const  lib_argv;
    const char *const *const  sim_argv;

    void *(*dlopen_fn)(const char *fn);
};

#ifdef __cplusplus
}
#endif

#endif /* COSIM_ABI_H */
