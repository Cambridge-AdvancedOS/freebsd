/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Performance Monitoring Unit
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_hwpmc_hooks.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#define	MAX_RLEN	8

struct pmu_softc {
	struct resource		*res[MAX_RLEN];
	device_t		dev;
	void			*ih[MAX_RLEN];
};

static struct ofw_compat_data compat_data[] = {
	{"arm,armv8-pmuv3",	1},
	{"arm,cortex-a17-pmu",	1},
	{"arm,cortex-a15-pmu",	1},
	{"arm,cortex-a12-pmu",	1},
	{"arm,cortex-a9-pmu",	1},
	{"arm,cortex-a8-pmu",	1},
	{"arm,cortex-a7-pmu",	1},
	{"arm,cortex-a5-pmu",	1},
	{"arm,arm11mpcore-pmu",	1},
	{"arm,arm1176-pmu",	1},
	{"arm,arm1136-pmu",	1},
	{"qcom,krait-pmu",	1},
	{NULL,			0}
};

static struct resource_spec pmu_spec[] = {
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		4,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		5,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		6,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		7,	RF_ACTIVE | RF_OPTIONAL },
	{ -1, 0 }
};

/* CCNT */
uint64_t ccnt_freq;
uint32_t ccnt_hi[MAXCPU];

#define	PMU_OVSR_C		0x80000000	/* Cycle Counter */
#define	PMU_IESR_C		0x80000000	/* Cycle Counter */

static int
pmu_intr(void *arg)
{
#ifdef HWPMC_HOOKS
	struct trapframe *tf;
#endif
	uint32_t r;
	u_int cpu;

	cpu = PCPU_GET(cpuid);

	r = cp15_pmovsr_get();
	if (r & PMU_OVSR_C) {
		atomic_add_32(&ccnt_hi[cpu], 1);
		/* Clear the event. */
		r &= ~PMU_OVSR_C;
		cp15_pmovsr_set(PMU_OVSR_C);
	}

#ifdef HWPMC_HOOKS
	/* Only call into the HWPMC framework if we know there is work. */
	if (r != 0 && pmc_intr) {
		tf = arg;
		(*pmc_intr)(PCPU_GET(cpuid), tf);
	}
#endif

	return (FILTER_HANDLED);
}

static int
pmu_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Performance Monitoring Unit");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
pmu_attach(device_t dev)
{
	struct pmu_softc *sc;
	uint32_t iesr;
	int err;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, pmu_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Setup interrupt handler */
	for (i = 0; i < MAX_RLEN; i++) {
		if (sc->res[i] == NULL)
			break;

		err = bus_setup_intr(dev, sc->res[i], INTR_MPSAFE | INTR_TYPE_MISC,
		    pmu_intr, NULL, NULL, &sc->ih[i]);
		if (err) {
			device_printf(dev, "Unable to setup interrupt handler.\n");
			return (ENXIO);
		}
	}

	/* Initialize to 0. */
	for (err = 0; err < MAXCPU; err++)
		ccnt_hi[err] = 0;

	/* Enable the interrupt to fire on overflow. */
	iesr = cp15_pminten_get();
	iesr |= PMU_IESR_C;
	cp15_pminten_set(iesr);	

	return (0);
}

static device_method_t pmu_methods[] = {
	DEVMETHOD(device_probe,		pmu_probe),
	DEVMETHOD(device_attach,	pmu_attach),
	{ 0, 0 }
};

static driver_t pmu_driver = {
	"pmu",
	pmu_methods,
	sizeof(struct pmu_softc),
};

static devclass_t pmu_devclass;

DRIVER_MODULE(pmu, simplebus, pmu_driver, pmu_devclass, 0, 0);


static void
_calibrate_ccnt_freq(void *arg __unused)
{
	uint64_t ccnt1, ccnt2;

	/*
	 *"Calibrate" here and provide ccnt_freq.  We have a ~4.2s window
	 * before the counter wraps around (we do not have interrupts to
	 * catch that yet, so make sure 2 > 1 :-).
	 */
	ccnt1 = cp15_pmccntr_get();
	DELAY(1000000);
	ccnt2 = cp15_pmccntr_get();
	if (ccnt2 < ccnt1)
		ccnt2 += 0x100000000;
	ccnt_freq = ccnt2 - ccnt1;
	if (bootverbose)
		printf("CCNT clock: %ju Hz\n", (intmax_t)ccnt_freq);
}
/* SI_SUB_FIRST so it runs before DTrace skew measure. */
SYSINIT(_calibrate_ccnt_freq, SI_SUB_SMP, SI_ORDER_FIRST, _calibrate_ccnt_freq, NULL);

