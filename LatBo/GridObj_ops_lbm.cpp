/* This file holds all the code for the core LBM operations including collision,
streaming and macroscopic calulcation.
*/

#include "stdafx.h"
#include "GridObj.h"
#include "definitions.h"
#include "globalvars.h"
#include "ivector.h"

using namespace std;


// ***************************************************************************************************

// LBM multi-grid kernel applicable for both single and multi-grid (IBM assumed on coarse grid for now)
// IBM_flag dictates whether we need the predictor step or not
void GridObj::LBM_multi ( bool IBM_flag ) {

	// Local stores used to hold info prior to IBM predictive step
	ivector<double> f_ibm_initial, u_ibm_initial, rho_ibm_initial;

	// If IBM on and predictive loop flag true then store initial data and reset forces
#ifdef IBM_ON
	// Limit to level 0 immersed body for now
	if (level == 0 && IBM_flag == true) {

		cout << "Prediction step..." << endl;

		// Store lattice data
		f_ibm_initial = f;
		u_ibm_initial = u;
		rho_ibm_initial = rho;

		// Reset lattice and Cartesian force vectors at each site
		LBM_forcegrid(true);
	}
#endif

	// Execute kernel as normal...

	// Loop twice on refined levels as refinement ratio per level is 2
	int count = 1;
	do {

		// Force lattice directions using current Cartesian force vector (adding gravity if necessary)
		LBM_forcegrid(false);

		// Collision on Lr
		if (level == 0) {

			// Collide on whole grid
			LBM_collide(false);
		
		} else {

			// Collide on core only (excludes upper transition layer)
			LBM_collide(true);

		}

		// Check if lower level exists
		if (NumLev > level) {

			size_t regions = subGrid.size();
			for (size_t reg = 0; reg < regions; reg++) {

				// Explode
				LBM_explode(reg);

				// Call same routine for lower level
				subGrid[reg].LBM_multi(IBM_flag);

			}

			// Apply boundary conditions
			LBM_boundary(2);	// Inlet/Outlet
			LBM_boundary(0);	// Bounce-back (walls and solids)

			// Stream
			LBM_stream();
			
			for (size_t reg = 0; reg < regions; reg++) {

				// Coalesce
				LBM_coalesce(reg);

			}
			
		} else {

			// Apply boundary conditions
			LBM_boundary(2);	// Inlet/Outlet
			LBM_boundary(0);	// Bounce-back (walls and solids)

			// Stream
			LBM_stream();
			
		}

		// Update macroscopic quantities
		LBM_macro();
		
		// Check if on L0 and if so drop out as only need to loop once on coarsest level
		if (level == 0) {
			break;
		}

		// Increment counter
		count++;

	} while (count < 3);


	// On completion of kernel...

	// Execute IBM procedure using newly computed predicted data
#ifdef IBM_ON
	if (level == 0 && IBM_flag == true) {

		// Interpolate velocity
		ibm_interpol();

		// Compute restorative force
		ibm_computeforce();

		// Reset force vectors
		LBM_forcegrid(true);

		// Spread force back to lattice (Cartesian vector)
		ibm_spread();

		// Restore data to start of time step
		f = f_ibm_initial;
		u = u_ibm_initial;
		rho = rho_ibm_initial;

		// Relaunch kernel with IBM flag set to false (corrector step)
		cout << "Correction step..." << endl;
		LBM_multi(false);

	}
#endif

}

// ***************************************************************************************************

// Takes Cartesian force vector and populates forces for each lattice direction.
// If reset_flag is true, resets the force vectors.
void GridObj::LBM_forcegrid(bool reset_flag) {

	/* This routine computes the forces applied along each direction on the lattice
	from Guo's 2002 scheme. The basic LBM must be modified in two ways: 1) the forces 
	are added to the populations produced by the LBGK collision in the collision 
	routine; 2) dt/2 * F is added to the momentum in the macroscopic calculation. This
	has been done already in the other routines.

	The forces along each direction are computed according to:

	F_i = (1 - 1/2*tau) * (w_i / cs^2) * ( (c_i - v) + (1/cs^2) * (c_i . v) * c_i ) . F

	where
	F_i = force applied to lattice in i-th direction
	tau = relaxation time
	w_i = weight in i-th direction
	cs = lattice sound speed
	c_i = vector of lattice speeds for i-th direction
	v = macroscopic velocity vector
	F = Cartesian force vector

	In the following, we assume
	lambda_i = (1 - 1/2*tau) * (w_i / cs^2)
	beta_i = (1/cs^2) * (c_i . v)

	The above in shorthand becomes summing over d dimensions:

	F_i = lambda_i * sum( F_d * (c_d_i (1+beta_i) - u_d )

	*/
	
	// Get grid sizes
	size_t N_lim = XPos.size();
	size_t M_lim = YPos.size();
	size_t K_lim = ZPos.size();

	// Declarations
	double lambda_v;

	// Loop over grid and overwrite forces for each direction
	for (size_t i = 0; i < N_lim; i++) {
		for (size_t j = 0; j < M_lim; j++) {
			for (size_t k = 0; k < K_lim; k++) {
				for (size_t v = 0; v < nVels; v++) {

					// Only apply to non-solid sites
					if (LatTyp(i,j,k,M_lim,K_lim) != 0) {

						if (reset_flag) {

							// Reset lattice force vectors
							force_i(i,j,k,v,M_lim,K_lim,nVels) = 0.0;

							// Reset Cartesian force vector
							for (unsigned int d = 0; d < dims; d++) {
								force_xyz(i,j,k,d,M_lim,K_lim,dims) = 0.0;
							}

						} else {
							// Else, compute forces

							// Reset beta_v
							double beta_v = 0.0;
#ifdef GRAVITY_ON
							// Add gravity (-y direction) to any IBM forces currently stored
							force_xyz(i,j,k,1,M_lim,K_lim,dims) -= rho(i,j,k,M_lim,K_lim) * acc_g;
#endif
							// Compute the lattice forces based on Guo's forcing scheme
							lambda_v = (1 - 0.5 * omega) * ( w[v] / pow(cs,2) );

							// Dot product (sum over d dimensions)
							for (unsigned int d = 0; d < dims; d++) {
								beta_v +=  (c[d][v] * u(i,j,k,d,M_lim,K_lim,dims));
							}
							beta_v = beta_v * (1/pow(cs,2));

							// Compute force using shorthand sum described above
							for (unsigned int d = 0; d < dims; d++) {
								force_i(i,j,k,v,M_lim,K_lim,nVels) += force_xyz(i,j,k,d,M_lim,K_lim,dims) * (c[d][v] * (1 + beta_v) - u(i,j,k,d,M_lim,K_lim,dims));
							}

							// Mulitply by lambda_v
							force_i(i,j,k,v,M_lim,K_lim,nVels) = force_i(i,j,k,v,M_lim,K_lim,nVels) * lambda_v;

						}

					}

				}
			}
		}
	}
	

}


// ***************************************************************************************************

// Collision operator
// Excludes the upper TL sites if core_flag set to true
void GridObj::LBM_collide( bool core_flag ) {

	/*
	Loop through the lattice points to compute the new distribution functions.
	Equilibrium based on:
	       rho * w * (1 + c_ia u_a / cs^2 + Q_iab u_a u_b / 2*cs^4
	*/

	// Declarations and Grid size
	int N_lim = XPos.size();
	int M_lim = YPos.size();
	int K_lim = ZPos.size();
	int i_low, j_low, k_low, i_high, j_high, k_high;

	// Respond to core flag
	if (core_flag) {
		// Ignore TL sites to upper level
		i_low = 2; i_high = N_lim-2;
		j_low = 2; j_high = M_lim-2;
#if (dims == 3)
		k_low = 2; k_high = K_lim-2;
#else
		k_low = 0; k_high = K_lim; // if 2D set to default
#endif

	} else {
		i_low = 0; i_high = N_lim;
		j_low = 0; j_high = M_lim;
		k_low = 0; k_high = K_lim;
	}

	// Create temporary lattice to prevent overwriting useful populations and initialise with same values as
	// pre-collision f grid. Initialise with current f values.
	ivector<double> f_new( f );

	// Loop over lattice sites
	for (int i = i_low; i < i_high; i++) {
		for (int j = j_low; j < j_high; j++) {
			for (int k = k_low; k < k_high; k++) {

				// Ignore refined sites
				if (LatTyp(i,j,k,M_lim,K_lim) == 2) {
					// Do nothing as taken care of on lower level grid

				} else {


					for (int v = 0; v < nVels; v++) {
						
						// Get feq value by calling overload of collision function
						feq(i,j,k,v,M_lim,K_lim,nVels) = LBM_collide( i, j, k, v );						
						
						// Recompute distribution function f
						f_new(i,j,k,v,M_lim,K_lim,nVels) = (-omega * (f(i,j,k,v,M_lim,K_lim,nVels) - feq(i,j,k,v,M_lim,K_lim,nVels)) ) + f(i,j,k,v,M_lim,K_lim,nVels)
															+ force_i(i,j,k,v,M_lim,K_lim,nVels);
						
					}

				}

			}
		}
	}

	// Update f from fnew
	f = f_new;

}


// Overload of collision function to allow calculation of feq only for initialisation
double GridObj::LBM_collide( int i, int j, int k, int v ) {

	/* LBGK equilibrium function is represented as:
		feq_i = rho * w_i * ( 1 + u_a c_ia / cs^2 + Q_iab u_a u_b / 2*cs^4 )
	where
		Q_iab = c_ia c_ib - cs^2 * delta_ab
	and
		delta_ab is the Kronecker delta.
	*/

	// Declare single feq value and intermediate values A and B
	double feq, A, B;

	// Other declarations
	int N_lim = XPos.size();
	int M_lim = YPos.size();
	int K_lim = ZPos.size();
	
	// Compute the parts of the expansion for feq (we now have a dot product routine so could simplify this code)

#if (dims == 3)
		// Compute c_ia * u_a which is actually the dot product of c and u
		A = (c[0][v] * u(i,j,k,0,M_lim,K_lim,dims)) + (c[1][v] * u(i,j,k,1,M_lim,K_lim,dims)) + (c[2][v] * u(i,j,k,2,M_lim,K_lim,dims));

		/*
		Compute second term in the expansion
		Q_iab u_a u_b = 
		(c_x^2 - cs^2)u_x^2 + (c_y^2 - cs^2)u_y^2 + (c_z^2 - cs^2)u_z^2
		+ 2c_x c_y u_x u_y + 2c_x c_z u_x u_z + 2c_y c_z u_y u_z
		*/

		B =	(pow(c[0][v],2) - pow(cs,2)) * pow(u(i,j,k,0,M_lim,K_lim,dims),2) + 
			(pow(c[1][v],2) - pow(cs,2)) * pow(u(i,j,k,1,M_lim,K_lim,dims),2) +
			(pow(c[2][v],2) - pow(cs,2)) * pow(u(i,j,k,2,M_lim,K_lim,dims),2) +
			2 * c[0][v]*c[1][v] * u(i,j,k,0,M_lim,K_lim,dims) * u(i,j,k,1,M_lim,K_lim,dims) + 
			2 * c[0][v]*c[2][v] * u(i,j,k,0,M_lim,K_lim,dims) * u(i,j,k,2,M_lim,K_lim,dims) + 
			2 * c[1][v]*c[2][v] * u(i,j,k,1,M_lim,K_lim,dims) * u(i,j,k,2,M_lim,K_lim,dims);
#else
		// 2D versions of the above
		A = (c[0][v] * u(i,j,k,0,M_lim,K_lim,dims)) + (c[1][v] * u(i,j,k,1,M_lim,K_lim,dims));

		B =	(pow(c[0][v],2) - pow(cs,2)) * pow(u(i,j,k,0,M_lim,K_lim,dims),2) + 
			(pow(c[1][v],2) - pow(cs,2)) * pow(u(i,j,k,1,M_lim,K_lim,dims),2) +
			2 * c[0][v]*c[1][v] * u(i,j,k,0,M_lim,K_lim,dims) * u(i,j,k,1,M_lim,K_lim,dims);
#endif
	
	
	// Compute f^eq
	feq = rho(i,j,k,M_lim,K_lim) * w[v] * (1 + (A / pow(cs,2)) + (B / (2*pow(cs,4))));

	return feq;

}


// ***************************************************************************************************

// Streaming operator
// Applies periodic BCs on level 0
void GridObj::LBM_stream( ) {

	// Declarations
	int N_lim = XPos.size();
	int M_lim = YPos.size();
	int K_lim = ZPos.size();
	int dest_x, dest_y, dest_z;

	// Create temporary lattice of zeros to prevent overwriting useful populations
	ivector<double> f_new( f.size(), 0.0 );

	// Stream one lattice site at a time
	for (int i = 0; i < N_lim; i++) {
		for (int j = 0; j < M_lim; j++) {
			for (int k = 0; k < K_lim; k++) {

				for (int v = 0; v < nVels; v++) {

					// If fine site then do not stream in any direction
					if (LatTyp(i,j,k,M_lim,K_lim) == 2) {
						break;
					}

					// Only apply periodic BCs on coarsest level
					if (level == 0) {
						// Compute destination coordinates
						dest_x = (i+c[0][v] + N_lim) % N_lim;
						dest_y = (j+c[1][v] + M_lim) % M_lim;
						dest_z = (k+c[2][v] + K_lim) % K_lim;
					} else {
						// No periodic BCs
						dest_x = i+c[0][v];
						dest_y = j+c[1][v];
						dest_z = k+c[2][v];
					}

					// If destination off-grid, do not stream
					if (	(dest_x >= N_lim || dest_x < 0) ||
							(dest_y >= M_lim || dest_y < 0) ||
							(dest_z >= K_lim || dest_z < 0)
						) {
						// Do nothing
				
					} else {

						// Check destination site type and decide whether to stream or not
						if ( (LatTyp(dest_x,dest_y,dest_z,M_lim,K_lim) == 2) || // Fine -- ignore
							( (LatTyp(dest_x,dest_y,dest_z,M_lim,K_lim) == 4) && (LatTyp(i,j,k,M_lim,K_lim) == 4) ) // TL lower level to TL lower level -- done on lower grid stream so ignore.
							) {
														
					
						} else {

							// Stream population
							f_new(dest_x,dest_y,dest_z,v,M_lim,K_lim,nVels) = f(i,j,k,v,M_lim,K_lim,nVels);

						}

					}

				}

			}
		}
	}

	// Replace old grid with new grid
	f = f_new;

}


// ***************************************************************************************************

// Macroscopic quantity calculation
void GridObj::LBM_macro( ) {

	// Declarations
	int N_lim = XPos.size();
	int M_lim = YPos.size();
	int K_lim = ZPos.size();
	double rho_temp = 0;
	double fux_temp = 0;
	double fuy_temp = 0;
	double fuz_temp = 0;

	// Loop over lattice
	for (int i = 0; i < N_lim; i++) {
		for (int j = 0; j < M_lim; j++) {
			for (int k = 0; k < K_lim; k++) {

				// Reset temporary variables
				rho_temp = 0; fux_temp = 0; fuy_temp = 0; fuz_temp = 0;

				for (int v = 0; v < nVels; v++) {

					// Sum up to find momentum
					fux_temp += c[0][v] * f(i,j,k,v,M_lim,K_lim,nVels);
					fuy_temp += c[1][v] * f(i,j,k,v,M_lim,K_lim,nVels);
					fuz_temp += c[2][v] * f(i,j,k,v,M_lim,K_lim,nVels);

					// Sum up to find density
					rho_temp += f(i,j,k,v,M_lim,K_lim,nVels);

				}

				// Assign density
				rho(i,j,k,M_lim,K_lim) = rho_temp;

				// Add forces to momentum
				fux_temp += (dt/2) * force_xyz[0];
				fuy_temp += (dt/2) * force_xyz[1];
				fuz_temp += (dt/2) * force_xyz[2];

				// Assign velocity
				u(i,j,k,0,M_lim,K_lim,dims) = fux_temp / rho_temp;
				u(i,j,k,1,M_lim,K_lim,dims) = fuy_temp / rho_temp;
#if (dims == 3)
				u(i,j,k,2,M_lim,K_lim,dims) = fuz_temp / rho_temp;
#endif

			}
		}
	}

#ifdef SOLID_ON
	// Do a solid site reset of velocity
	solidSiteReset();
#endif

}


// ***************************************************************************************************

// Explosion operation
void GridObj::LBM_explode( int RegionNumber ) {

	// Declarations
	int y_start, x_start, z_start;
	int N_fine = subGrid[RegionNumber].YPos.size();
	int M_fine = subGrid[RegionNumber].YPos.size();
	int K_fine = subGrid[RegionNumber].ZPos.size();
	int N_coarse = XPos.size();
	int M_coarse = YPos.size();
	int K_coarse = ZPos.size();

	// Loop over coarse grid (just region of interest)
	for (size_t i = subGrid[RegionNumber].CoarseLimsX[0]; i <= subGrid[RegionNumber].CoarseLimsX[1]; i++) {
		for (size_t j = subGrid[RegionNumber].CoarseLimsY[0]; j <= subGrid[RegionNumber].CoarseLimsY[1]; j++) {
			for (size_t k = subGrid[RegionNumber].CoarseLimsZ[0]; k <= subGrid[RegionNumber].CoarseLimsZ[1]; k++) {

				// If TL to lower level and point belongs to region then partitioning required
				if (LatTyp(i,j,k,M_coarse,K_coarse) == 4) {

					// Lookup indices for lower level
					x_start = subGrid[RegionNumber].CoarseLimsX[0];
					y_start = subGrid[RegionNumber].CoarseLimsY[0];
					z_start = subGrid[RegionNumber].CoarseLimsZ[0];

					// Find indices of fine site
					vector<int> idx_fine = indmapref(i, x_start, j, y_start, k, z_start);
					int fi = idx_fine[0];
					int fj = idx_fine[1];
					int fk = idx_fine[2];

					// Update fine grid values according to Rohde et al.
					for (int v = 0; v < nVels; v++) {

						// Get coarse site value
						double coarse_f = f(i,j,k,v,M_coarse,K_coarse,nVels);

#if (dims == 3)
						// 3D Case -- cube of 8 cells
			
						// Copy coarse to fine
						subGrid[RegionNumber].f(fi,		fj,		fk,		v,M_fine,K_fine,nVels)	= coarse_f;
						subGrid[RegionNumber].f(fi+1,	fj,		fk,		v,M_fine,K_fine,nVels)	= coarse_f;
						subGrid[RegionNumber].f(fi,		fj+1,	fk,		v,M_fine,K_fine,nVels)	= coarse_f;
						subGrid[RegionNumber].f(fi+1,	fj+1,	fk,		v,M_fine,K_fine,nVels)	= coarse_f;
						subGrid[RegionNumber].f(fi,		fj,		fk+1,	v,M_fine,K_fine,nVels)	= coarse_f;
						subGrid[RegionNumber].f(fi+1,	fj,		fk+1,	v,M_fine,K_fine,nVels)	= coarse_f;
						subGrid[RegionNumber].f(fi,		fj+1,	fk+1,	v,M_fine,K_fine,nVels)	= coarse_f;
						subGrid[RegionNumber].f(fi+1,	fj+1,	fk+1,	v,M_fine,K_fine,nVels)	= coarse_f;

#else

						// 2D Case -- square of 4 cells
							
						// Copy coarse to fine
						subGrid[RegionNumber].f(fi,		fj,		v,M_fine,nVels)		= coarse_f;
						subGrid[RegionNumber].f(fi+1,	fj,		v,M_fine,nVels)		= coarse_f;
						subGrid[RegionNumber].f(fi,		fj+1,	v,M_fine,nVels)		= coarse_f;
						subGrid[RegionNumber].f(fi+1,	fj+1,	v,M_fine,nVels)		= coarse_f;

#endif
					}

				}

			}
		}
	}


}


// ***************************************************************************************************

// Coalesce operation -- called from coarse level
void GridObj::LBM_coalesce( int RegionNumber ) {

	// Declarations
	int y_start, x_start, z_start;
	int N_fine = subGrid[RegionNumber].YPos.size();
	int M_fine = subGrid[RegionNumber].YPos.size();
	int K_fine = subGrid[RegionNumber].ZPos.size();
	int N_coarse = XPos.size();
	int M_coarse = YPos.size();
	int K_coarse = ZPos.size();

	// Loop over coarse grid (only region of interest)
	for (size_t i = subGrid[RegionNumber].CoarseLimsX[0]; i <= subGrid[RegionNumber].CoarseLimsX[1]; i++) {
		for (size_t j = subGrid[RegionNumber].CoarseLimsY[0]; j <= subGrid[RegionNumber].CoarseLimsY[1]; j++) {
			for (size_t k = subGrid[RegionNumber].CoarseLimsZ[0]; k <= subGrid[RegionNumber].CoarseLimsZ[1]; k++) {

				// If TL to lower level then fetch values from lower level
				if (LatTyp(i,j,k,M_coarse,K_coarse) == 4) {

					// Lookup indices for lower level
					x_start = subGrid[RegionNumber].CoarseLimsX[0];
					y_start = subGrid[RegionNumber].CoarseLimsY[0];
					z_start = subGrid[RegionNumber].CoarseLimsZ[0];

					// Find indices of fine site
					vector<int> idx_fine = indmapref(i, x_start, j, y_start, k, z_start);
					int fi = idx_fine[0];
					int fj = idx_fine[1];
					int fk = idx_fine[2];

					// Loop over directions
					for (int v = 0; v < nVels; v++) {

						// Check to see if f value is missing on coarse level
						if (f(i,j,k,v,M_coarse,K_coarse,nVels) == 0) {
																										
#if (dims == 3)
							// 3D Case -- cube of 8 cells
							
							// Average the values
							f(i,j,k,v,M_coarse,K_coarse,nVels) = (
								subGrid[RegionNumber].f(fi,		fj,		fk,		v,M_fine,K_fine,nVels) + 
								subGrid[RegionNumber].f(fi+1,	fj,		fk,		v,M_fine,K_fine,nVels) + 
								subGrid[RegionNumber].f(fi,		fj+1,	fk,		v,M_fine,K_fine,nVels) + 
								subGrid[RegionNumber].f(fi+1,	fj+1,	fk,		v,M_fine,K_fine,nVels) +
								subGrid[RegionNumber].f(fi,		fj,		fk+1,	v,M_fine,K_fine,nVels) + 
								subGrid[RegionNumber].f(fi+1,	fj,		fk+1,	v,M_fine,K_fine,nVels) + 
								subGrid[RegionNumber].f(fi,		fj+1,	fk+1,	v,M_fine,K_fine,nVels) + 
								subGrid[RegionNumber].f(fi+1,	fj+1,	fk+1,	v,M_fine,K_fine,nVels)
								) / pow(2, dims);

#else

							// 2D Case -- square of 4 cells
							
							// Average the values
							f(i,j,k,v,M_coarse,K_coarse,nVels) = (
								subGrid[RegionNumber].f(fi,		fj,		v,M_fine,nVels) + 
								subGrid[RegionNumber].f(fi+1,	fj,		v,M_fine,nVels) + 
								subGrid[RegionNumber].f(fi,		fj+1,	v,M_fine,nVels) + 
								subGrid[RegionNumber].f(fi+1,	fj+1,	v,M_fine,nVels) 
								) / pow(2, dims);

#endif
						}
					
					}

				}

			}
		}
	}

}

// ***************************************************************************************************