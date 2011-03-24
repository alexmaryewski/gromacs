#ifndef _qhop_h
#define _qhop_h

#ifdef HAVE_IDENT
#ident	"@(#) qhop.h 1 28/2/01"
#endif /* HAVE_IDENT */
#include <stdio.h>
#include "typedefs.h"
#include "pbc.h"
#include "network.h"
#include "tgroup.h"
#include "gmx_qhop_db.h"
#include "resall.h"

/*! \brief Initializes the qhoprec.
 * 
 * What it does:
 *   - Initializes qhoprec
 *   - Picks out the atoms that are titratable
 *   - Figures out which residue subtypes to use at step 0,
 *     based on the H existence map.
 *   - Creates a bqhopdonor (andbqhopacceptor) array in
 *     the t_mdatoms to be used by nbsearch,
 *   - Completes the qhop residues array
 *   - Reads in the hopping and force field parameters.
 * \return The number of titrating atoms.
 */
extern int init_qhop(FILE *fplog,const char *ff,
		     t_commrec *cr, gmx_mtop_t *mtop, 
		     t_inputrec *ir, const t_forcerec *fr,
		     matrix box, t_mdatoms *md);

/** \brief Identifies potential hops calculates probabilities.
 *
 * do_qhop() identifies acceptors in hopping range of the donors,
 * calculates relevant energies and subsequently probabilities for
 * every hop. The list of hops is scrambled according to the qhopmode
 * and the hops are tested against random numbers. The actual hopping
 * will be done later.
 */
extern void
do_qhop(FILE *fplog, 
	t_commrec *cr,
	t_inputrec *ir, 
	t_nrnb *nrnb,
	gmx_wallcycle_t wcycle, 
	gmx_localtop_t *top,
	gmx_mtop_t *mtop, 
	gmx_groups_t *groups,
	t_state *state,
	t_mdatoms *md, 
	t_fcdata *fcd,
	t_graph *graph, 
	t_forcerec *fr,
	gmx_vsite_t *vsite,
	rvec mu_tot,
	gmx_bool bBornRadii,
	real T,
	gmx_large_int_t step,
	gmx_ekindata_t *ekindata,
	tensor force_vir
	);

extern void qhop_stash_bonded(qhop_db_t db, gmx_mtop_t *mtop);


/* qhop_(de)protonate do nothing to the coordinates, they hack the topology. */

/**
 * \brief Protonates the qatom.
 * 
 * The qatom is protonated.
 */
extern void qhop_protonate(qhop_db *db, t_qhoprec *qr,
			   const t_inputrec *ir,
			   const t_commrec *cr, gmx_localtop_t *top,
			   gmx_constr_t constr,
			   t_qhop_atom *qatom,
			   t_mdatoms *md, gmx_bool bWater,
			   gmx_bool bSwapBondeds,
			   gmx_bool bRealHop);

extern void qhop_deprotonate(qhop_db *db, t_qhoprec *qr,
			     const t_inputrec *ir, const t_commrec *cr,
			     gmx_localtop_t *top, gmx_constr_t constr,
			     t_qhop_atom *qatom,
			     t_mdatoms *md, gmx_bool bWater,
			     gmx_bool bSwapBondeds,
			     gmx_bool bRealHop);

/* Goes through the t_ilist and finds the bonded interactions
 * that can be changed */
extern void qhop_index_bondeds(t_ilist *ilist, qhop_db_t db,
			       t_qhoprec *qr, gmx_bool bGlobal);

extern void fold_inactive_protons(const t_qhoprec *qr, rvec x[], rvec v[]);

#endif	/* _qhop_h */
