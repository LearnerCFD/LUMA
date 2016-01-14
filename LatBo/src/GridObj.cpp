/* This file contains the constructors for the Grid objects. */

#include "../inc/stdafx.h"
#include "../inc/definitions.h"
#include "../inc/GridObj.h"
#include "../inc/MpiManager.h"
#include <vector>

// Static declarations
std::ofstream* GridUtils::logfile;
int MpiManager::my_rank;
int MpiManager::num_ranks;

// ***************************************************************************************************
// Default Constructor
GridObj::GridObj(void)
{
}

// ***************************************************************************************************
// Default Destructor
GridObj::~GridObj(void)
{
}

// ***************************************************************************************************
// Basic Constructor for top level grid
GridObj::GridObj(int level)
{
	// Defaults
	this->t = 0;

	// Assign level and region number
	this->level = level;
	this->region_number = 0;
	// Set limits of refinement to zero as top level
	for (int i = 0; i < 2; i++) {
		this->CoarseLimsX[i] = 0;
		this->CoarseLimsY[i] = 0;
		this->CoarseLimsZ[i] = 0;
	}

	*GridUtils::logfile << "Constructing Grid level " << level  << std::endl;

	// Call L0 non-MPI initialiser
	this->LBM_init_grid();

}

// ***************************************************************************************************
// MPI constructor for level 0 with grid level, rank, local grid size and its global edges
GridObj::GridObj(int level, std::vector<unsigned int> local_size, 
				 std::vector< std::vector<unsigned int> > GlobalLimsInd, 
				 std::vector< std::vector<double> > GlobalLimsPos)
{
	// Assign
	this->level = level;
    this->region_number = 0;
	this->t = 0;
	// Set limits of refinement to zero as top level
	for (int i = 0; i < 2; i++) {
		this->CoarseLimsX[i] = 0;
		this->CoarseLimsY[i] = 0;
		this->CoarseLimsZ[i] = 0;
	}

	*GridUtils::logfile << "Building Grid level " << level << " on rank " << MpiManager::my_rank << std::endl;

	// Call MPI initialisation routine
	LBM_init_grid(local_size, GlobalLimsInd, GlobalLimsPos); 

}

// ***************************************************************************************************
// Overload constructor for a sub grid
GridObj::GridObj(int RegionNumber, GridObj& pGrid)
{

	// Assign
	this->level = pGrid.level + 1;
    this->region_number = RegionNumber;
	this->t = 0;

	*GridUtils::logfile << "Building Grid level " << level << ", region " << region_number << ", on rank " << MpiManager::my_rank << std::endl;

	/* Store coarse grid refinement limits on the new sub-grid
	 *
	 * These are stored as local indices as they are used to map between the 
	 * fine and coarse grid cells during multi-grid operations. Therefore, we 
	 * must only store local values relevant to the grid on the rank and not the
	 * refined region as a whole or mapping will not be correct.
	 */
	
	// Get limits from the definitions file -- convert to local indices
	CoarseLimsX[0] = RefXstart[pGrid.level][RegionNumber] - pGrid.XInd[1] + 1;	CoarseLimsX[1] = RefXend[pGrid.level][RegionNumber] - pGrid.XInd[1] + 1;
	CoarseLimsY[0] = RefYstart[pGrid.level][RegionNumber] - pGrid.YInd[1] + 1;	CoarseLimsY[1] = RefYend[pGrid.level][RegionNumber] - pGrid.YInd[1] + 1;
#if (dims == 3)
	CoarseLimsZ[0] = RefZstart[pGrid.level][RegionNumber] - pGrid.ZInd[1] + 1;	CoarseLimsZ[1] = RefZend[pGrid.level][RegionNumber] - pGrid.ZInd[1] + 1;
#endif

}

// ***************************************************************************************************
// Method to generate a subgrid
void GridObj::LBM_addSubGrid(int RegionNumber) {

	// Return if no subgrid required for the current grid
	if ( !GridUtils::hasThisSubGrid(*this, RegionNumber) ) return;

	// Ok to proceed and add the subgrid passing parent grid as a reference for initialisation
	subGrid.emplace_back( RegionNumber, *this);	

	// Initialise the subgrid passing position of corner of the refined region on parent grid
	this->subGrid[subGrid.size()-1].LBM_init_subgrid(*this);

	// Add another subgrid beneath the one just created if necessary
	if (this->subGrid[subGrid.size()-1].level < NumLev) {
		this->subGrid[subGrid.size()-1].LBM_addSubGrid(this->subGrid[subGrid.size()-1].region_number);
	}

}

// ***************************************************************************************************
// ***************************************************************************************************
// Other member methods are in their own files prefixed GridObj_