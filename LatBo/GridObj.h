#pragma once

#include <vector>
#include "ivector.h"
#include "IB_body.h"

// Base class
class GridObj
{

public:

	GridObj( ); // Default constructor
	GridObj(int level); // Constructor with level
	GridObj(int level, int RegionNumber); // Sub grid constructor
	~GridObj( ); // Default destructor


	/*	
	***************************************************************************************************************
	********************************************* Member Data *****************************************************
	***************************************************************************************************************
	*/
	
private :

	// 1D subgrid array (size = NumReg)
	std::vector<GridObj> subGrid;
	
	// Start and end indices of corresponding coarse level (unsigned integers)
	size_t CoarseLimsX[2];
	size_t CoarseLimsY[2];
	size_t CoarseLimsZ[2];

	// 1D arrays
	std::vector<int> XInd; // Vectors of indices
	std::vector<int> YInd;
	std::vector<int> ZInd;
	std::vector<double> XPos; // Vectors of positions of sites
	std::vector<double> YPos;
	std::vector<double> ZPos;

	// Vector nodal properties
	// Flattened 4D arrays (i,j,k,vel)
	ivector<double> f;
	ivector<double> feq;
	ivector<double> u;
	ivector<double> force_xyz;
	ivector<double> force_i;

	// Scalar nodal properties
	// Flattened 3D arrays (i,j,k)
	ivector<double> rho;
	ivector<int> LatTyp;

	// Grid scalars
	double omega;
	double dx;
	double dy;
	double dz;
	int level;
	int region_number;

	// IBM objects
	IB_body iBody;	// Admit single immersed body for the moment


	// Public data members
public :

	double dt;	// Time step
	double Re;	// Reynolds number

	/*	
	***************************************************************************************************************
	********************************************* Member Methods **************************************************
	***************************************************************************************************************
	*/

public :

	// Initialisation functions
	void LBM_init_vel();		// Initialise the velocity field
	void LBM_init_rho();		// Initialise the density field
	void LBM_init_grid();		// Initialise top level grid with a velocity and denstiy field
	void LBM_init_subgrid(double offsetX, double offsetY, double offsetZ, double dx0, double omega_coarse);	// Initialise subgrid with all quantities
	void LBM_init_wall_lab();		// Initialise labels for objects and walls

	// LBM operations
	void LBM_multi(bool IBM_flag);				// Launch the multi-grid kernel
	void LBM_collide(bool core_flag);			// Apply collision + 1 overload
	double LBM_collide(int i, int j, int k, int v);
	void LBM_stream();							// Stream populations
	void LBM_macro();							// Compute macroscopic quantities
	void LBM_boundary(int bc_type_flag);		// Apply boundary conditions
	void LBM_forcegrid(bool reset_flag);		// Apply a force to the grid points (or reset vectors if flag is true)
	
	// Boundary operations
	void applyZouHe(int label, int i, int j, int k, int M_lim, int K_lim);	// Application of Zou-He procedure
	void solidSiteReset();	// Reset all the solid site velocities to zero

	// Multi-grid operations
	void LBM_explode(int RegionNumber);			// Explode populations from coarse to fine
	void LBM_coalesce(int RegionNumber);		// Coalesce populations from fine to coarse
	void LBM_addSubGrid(int RegionNumber);		// Add and initialise subgrid structure for a given region number

	// IO methods
	void LBM_textout(int t);		// Writes out the contents of the class as well as any subgrids to a text file
	// EnsightGold methods
	void genCase(int nsteps);		// Generate case file
	void genGeo();					// Generate geometry file
	void genVec(int fileNum);		// Generate vectors file
	void genScal(int fileNum);		// Generate scalars file
	// VTK writer methods
	void vtk_writer(int t, double tval);

	// IBM methods
	void build_body(int type);								// Build a new pre-fab body
	void ibm_initialise();									// Initialise a built immersed body
	double ibm_deltakernel(double rad, double dilation);	// Evaluate kernel (delta function approximation)
	void ibm_interpol();									// Interpolation of velocity field
	void ibm_spread();										// Spreading of restoring force
	void ibm_findsupport(unsigned int m);					// Populates support information for the m-th marker of a body
	void ibm_computeforce();								// Compute restorative force at each marker in a body
	void ibm_findepsilon();									// Method to find epsilon weighting parameter
	// Biconjugate gradient stablised method for solving asymmetric linear system required by finding epsilon
	double ibm_bicgstab(std::vector< std::vector<double> >& Amatrix, std::vector<double>& bVector, std::vector<double>& epsilon, 
						   double tolerance, unsigned int maxiterations);


	
};
