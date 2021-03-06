/**
 * \file    NVT.cpp
 * \author  James Sturgis
 * \date    April 6, 2018
 * \version 1.0
 * \brief   Run a trajectory in the NVT ensemble.
 *
 * This file contains the main routine for the NVT program that is part of
 * the Very Coarse Grained disc simulation programmes.
 *
 * The programme loads a configuration and then runs a monte carlo integration
 * in the NVT ensemble in which for each move a random object is selected and
 * moved to a new location and rotated to a new orientation. This modified
 * configuration is accepted according to the Metropolis criterion, the
 * energy is lower or the probability of the move at the simulation temperature,
 * calculated as e^(-dE beta), is higher than a uniform variant on the interval
 * 0..1. In this equation dE is the energy difference between the old and new
 * configurations, and beta (which is a parameter to the program) is 1/ kb T
 * where kb is the boltzman constant and T the absolute temperature.
 *
 * To use the program the command line is:
 *
 *      NVT n_steps print_frequency beta pressure initial_config final_config
 *
 * Where the various parameters are:
 *      n_steps         The number of simulation steps to make.
 *      print_frequency The number of steps between reports to the log file
 *                      of how the integration is progressing.
 *      beta            The temperature parameter 1/(kb T) that scales the
 *                      force field energies.
 *      pressure        The pressure (this is not used but is for compatibility
 *                      with other ensembles such as NPT or the Gibbs ensemble.
 *      initial_config  The name of an existing file containing a valid
 *                      configuration, that is read as the starting point.
 *      final_config    The name of a file to which will be written the final
 *                      configuration, if a file with this name exists already
 *                      it is deleted.
 *
 * The program does not use the standard input stream, but writes a log of progress
 * to the standard output stream (this can /should be redirected to a log file).
 * debugging and error messages are written to the standard error stream. The
 * program ends with the standard exit codes EXIT_SUCCESS or EXIT_FAILURE.
 *
 * Log file format:
 * The format of the log file is determined in this file by the print statements:
 *      lines 196-201   After loading the file.
 *      lines 228-232   After the initial adjustments
 *      lines 249-257   Every print_frequency steps during the integration.
 *      line 268        At the end of the program.
 *
 * Each report, except the last, contains 3 lines of slightly variable content.
 *
 * \todo log file       Use a dedicated function for writing data so it is easier
 *                      to parse after and control the structure.  Perhaps in
 *                      xml format.
 *
 * Configuration file format:
 * The configuration is read by the routine in config.cpp, and then object.cpp
 * It has a simple format:
 *      line 1          The area x and y dimensions.
 *      line 2          The number of objects.
 *      line 3-n        For each object four numbers:
 *                          the type (this refers to the topology)
 *                          the x and y positions should be in the area.
 *                          the orientation in radians.
 *
 * \todo config         Include non-rectangular surfaces in file.
 * \todo config         Include info on boundary conditions.
 * \todo config         Include comments.
 *
 * Topology:
 * The topology describes the relationship between objects and their constituent
 * atoms. It is currently hard coded in the file topology.cpp, lines 28-40. The
 * len[] array and data[][] matrix. The array len[] describes for each object i
 * the number of atoms it contains. The data matrix [][] in valid positions i, j
 * contains an atom with the properties (in order): atom_type, x_position,
 * y_position. The atom type refers to the atom_types used in the force_field file
 * and the positions are relative to the object position, which should be the
 * center of mass, at an orientation of 0.0 radians (or degrees).
 *
 * \todo topology       Read topology data from a file.
 *
 * Force field:
 * The force field describes the interactions between the different atom types.
 * Currently the force field is hard coded in the file force_field.cpp, lines
 * 11 to 21. The meaning of the different parts are as follows:
 *      BIGVALUE        A large finite number, used in place of infinity to avoid
 *                      NaN errors it should be less than MAX_DOUBLE divided by
 *                      twice the number of objects.
 *      my_radius[]     An array for each atom type of the hard radius of the
 *                      atom.
 *      my_color[]      An array for each atom type of the color to use for the
 *                      atom when drawing it to postscript (config2eps).
 *      my_energy[][]   A (symmetric) array of the potentiel well depths for
 *                      interactions between two different types of atom.
 *      my_cut_off      The distance between objects beyond which the interaction
 *                      energy is presumed to be 0.
 *      my_length       The length scale for interactions.
 *
 * \todo force_field    Convert lengths to by interaction basis (a matrix).
 * \todo force_field    Read force_field from a control file or a force field file.
 */

#include <cstdlib>
#include "integrator.h"
#include "common.h"

using namespace std;

#define fatal_error(format, value) {\
                    fprintf(stderr, format, value ); \
                    usage(); \
                    exit(EXIT_FAILURE); \
                }

void usage(){
    fprintf(stderr, "Usage: NVT %s\n",
        "n_steps print_frequency beta pressure initial_config final_config");
}

/*
 *
 */
int main(int argc, char** argv) {
    char        *fname;
    config      *current_state;
    config      **state_h;
    force_field *the_forces = new force_field(); // This memory is lost
    integrator  *the_integrator = NULL;
    topology    *a_topology;

    FILE        *src1;
    FILE        *dest1;
    FILE        *the_log;

    int         N1;
    double      U1, V1;
    int         i, step;

    int         it_max  =  10000;
    int         n_print =   1000;
    double      beta    =    1.0;
    double      dl_max  =  100.0;
    double      P1      =    1.0;

    // Initialization

    srand((long)&argv[0]);
    the_log = stderr;

    // Handle command line

    /***************************************************************************
     *  TODO: Reorganize command line handling to allow 'flag value' syntax
     *        with defaults.
     **************************************************************************/
    if(argc != 7){
        fatal_error("Wrong number of arguments: %d but expected 6\n", argc-1);
    }
    ++argv;
    it_max = atoi(*argv);
    if (it_max<1) fatal_error("Too few iterations: %d\n", it_max);

    ++argv;
    n_print = atoi(*argv);

    ++argv;
    beta = atof(*argv);

    ++argv;
    P1 = atof(*argv);

    ++argv;
    fname = *argv;
    if(! (src1 = fopen(fname, "r")))
        fatal_error("Unable to open %s for reading\n", fname );

    ++argv;
    fname = *argv;
    if(! (dest1 = fopen(fname, "w")))
        fatal_error("Unable to open %s for writing\n", fname );

    the_log = stdout;

    a_topology = new topology();    // Create or load the object topologies

    // Load the initial configuration
    current_state = new config(src1);
                                    // Add the topology to the configuration.
    current_state->add_topology(a_topology);

    U1 = current_state->energy(the_forces);
    V1 = current_state->area();
    N1 = current_state->n_objects();

    // Print report of state
    fprintf( the_log, "Configuration loaded\n");
    fprintf( the_log, "N objects = %9d Pressure = %9g   Beta = %9g\n",
            N1, P1, beta);
    fprintf( the_log, "Area      = %9g  Density = %9g Energy = %9g\n",
            V1, N1/V1, U1);

    dl_max = min(current_state->x_size, current_state->y_size)/2.0;

    // Jiggle everything to remove bad contacts from save/load

    i = 0;          // Counter for number of shifts.
    while(U1>the_forces->big_energy){
        if( i> 2000*N1 ){
            fatal_error(
                "Unable to adjust initial configuration in %d steps", i );
        }
        the_integrator = new integrator(the_forces);
        the_integrator->dl_max = dl_max;
        state_h = &current_state;
        the_integrator->run(state_h, beta, P1, 2*N1);
        current_state = *state_h;
        dl_max = the_integrator->dl_max;
        i += 2*N1;

        U1 = current_state->energy(the_forces);
    }


    if( the_integrator ){
        delete the_integrator;
        i = 0;
        fprintf( the_log, "After initial adjustments:\n");
        fprintf( the_log, "N objects = %9d Pressure = %9g   Beta = %9g\n",
            N1, P1, beta);
        fprintf( the_log, "Area      = %9g  Density = %9g Energy = %9g\n",
            V1, N1/V1, U1);
    }

    // Start NVT montecarlo loop
    step = min(n_print,it_max);
    the_integrator = new integrator(the_forces);
    the_integrator->dl_max = dl_max;

    for(i=0;i<it_max;i+=step){
        state_h = &current_state;
        the_integrator->run(state_h, beta, P1, step);
        current_state = *state_h;

        U1 = current_state->energy(the_forces);
        V1 = current_state->area();
        N1 = current_state->n_objects();

        fprintf(the_log, "After %d steps N = %d, P = %g, beta = %g\n",
                i+step, N1, P1, beta );
        fprintf(the_log, "Area = %g, Density = %g Energy = %g\n",
                V1, N1/V1, U1);
        fprintf(the_log, "Moves %d in %d, Dist_max = %g\n",
                the_integrator->n_good,
                the_integrator->n_good + the_integrator->n_bad,
                the_integrator->dl_max );

        step = min(step,it_max-i);
    }
    delete the_integrator;
    // Update log
    // Save result
    current_state->write(dest1);
    // Clean up
    delete current_state;
    delete the_forces;

    fprintf(the_log, "\n...Done...\n");
    fclose(the_log);

    return 0;
}
