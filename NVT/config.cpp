/**
 * @file        config.cpp  Implementation of the configuration class.
 * @author      James Sturgis
 * @date        April 6, 2018
 * @version     1.0
 */

#include <float.h>
#include <math.h>
#include "config.h"
#include "common.h"

/**
 * Constructor that produces an empty basic configuration. This is not
 * currently much use as there are not all the necessary functions for
 * manipulating the configuration.
 */
config::config() {
    x_size       = 1.0;
    y_size       = 1.0;
    unchanged    = true;
    saved_energy = 0.0;
    obj_list     = o_list();
    the_topology = (topology *)NULL;
    is_periodic  = false;
}

/**
 * Constructor that reads the configuration from a file. The format of this
 * file is described in the class description.
 *
 * @param src   An file descriptor open for reading that contains the
 *              configuration to be read.
 *
 * @todo        Define file format for configuration. Currently:
 *              x_size, y_size \n
 *              n_objects \n
 *              o_type x_pos y_pos rotation \n one line per object.
 *              Would like to:
 *              - Include comments
 *              - Be space tolerant
 *              - Have a default rotation for backward compatibility.
 * @todo        Handle errors in the input file or file reading in a sensible
 *              way. If there is an error this should be apparent even if the
 *              structure is still valid.
 * @todo        Read from file if periodic conditions or not.
 * @todo        Integrate an object constructor from a file saves 4 variables
 *              and a couple of lines of code.
 */
config::config(FILE *src) {
    int     n_obj;
    int     o_type,
            i;
    double  x_pos, y_pos, angle;
    object  *my_obj;

    fscanf(src,"%lf %lf\n", &x_size, &y_size);      // Read the configuration size.
    fscanf(src,"%d\n", &n_obj );                    // Read the number of objects
                                                    // in the configuration.
    obj_list = o_list();                            // Make a new obj_list (is this
                                                    // necessary?)
    for(i=0; i<n_obj; i++ ){                        // Loop over the objects.
                                                    // These 2 lines should really
                                                    // Use an object constructor.
        fscanf(src, "%d %lf %lf %lf\n", &o_type, &x_pos, &y_pos, &angle );
        my_obj = new object(o_type, x_pos, y_pos, angle );
        obj_list.add(my_obj);                       // Add the new object to the list.
    }                                               // End of loop over objects.

    unchanged = false;                              // Set up so will calculate energy.
    saved_energy = 0.0;
    the_topology = (topology *)NULL;                // Topologies are not included in
                                                    // the file.
    is_periodic = true;                             // This should be read from file.

    assert(n_obj == n_objects() );                  // Should check the configuration
                                                    // is alright.
}

/**
 * Copy constructor
 * @param orig the original configuration to be copied.
 */
config::config(config& orig) {
    object  *my_obj1;
    object  *my_obj2;

    x_size         = orig.x_size;
    y_size         = orig.y_size;
    saved_energy   = orig.saved_energy;
    unchanged      = orig.unchanged;
    the_topology   = new topology( /*orig.the_topology*/ ); // Cheat as topology hard coded
    is_periodic    = orig.is_periodic;
    obj_list.empty();
    for(int i = 0; i < orig.n_objects(); i++){
        my_obj1 = orig.obj_list.get(i);
        my_obj2 = new object( my_obj1->o_type, my_obj1->pos_x,
                my_obj1->pos_y, my_obj1->orientation);
        obj_list.add(my_obj2);
    }
}

/**
 * Destructor. Destroy the configuration releasing memory. As constructor
 * uses new probably need explicit destroy.
 */
config::~config() {
    if(the_topology) delete(the_topology);
}

/**
 * @return The area of the configuration.
 */
double config::area() { return x_size * y_size;}

/**
 * This function calculates the energy of a configuration by comparing using
 * the force field interaction function to measure the energy between pairs of
 * objects. As both indexes run from 0 to the end all interactions are counted
 * twice. This is done so that neighbour lists, if implemented, will work more
 * easilly. To increase the energy the object recalculate flag, and the
 * configuration unchanged flag are checked to reduce unnecessary evaluations
 * as long as these flags are correctly and efficiently updated.
 *
 * @param  the_force the force field to use for the energy calculation.
 * @return the total interaction energy between all object pairs.
 * @todo   Handle periodic conditions.
 */
double config::energy(force_field *&the_force) {
    int     i1, i2;                         // Two counters
    double  value = 0.0;                    // An accumulator that starts at 0.0
    object  *my_obj1;                       // Two object pointers

    if (! unchanged) {                      // Only if necessary
        saved_energy = 0.0;                 // Loop over the objects
                                            // This code needs optimizing.
        for(i1 = 0; i1 < obj_list.size(); i1++ ){
            my_obj1 = obj_list.get(i1);
            if( my_obj1->recalculate ){
                value = 0.0;
                for(i2 = 0; i2<obj_list.size(); i2++ ){
                    if(i1 != i2){            // If periodic then
                        double r, r2;
                        double dx = 0.0;
                        double dy = 0.0;

                        object  *my_obj2 = obj_list.get(i2);
                        if(is_periodic){     // Move my_obj2 to closest image
                                             // Check this code...
                            r  = my_obj2->pos_x - my_obj1->pos_x;
                            dx = (r<0)?x_size:-x_size;
                            r2 = r + dx;
                            dx = (abs(r2)<abs(r))?dx:0.0;
                            my_obj2->pos_x += dx;

                            r  = my_obj2->pos_y - my_obj1->pos_y;
                            dy = (r<0)?y_size:-y_size;
                            r2 = r + dy;
                            dy = (abs(r2)<abs(r))?dy:0.0;
                            my_obj2->pos_y += dy;
                        }
                        value += my_obj1->interaction( the_force,
                                the_topology, my_obj2 );
                        if(is_periodic){    // And move back again.
                            my_obj2->pos_x -= dx;
                            my_obj2->pos_y -= dy;
                        }
                    }
                }                           // Calculate interaction with wall
                if(! is_periodic ){         // if not periodic conditions.
                    value += my_obj1->box_energy( the_force, the_topology,
                            x_size, y_size );
                }
                my_obj1->set_energy(value); // Set the energy of the object
            }                               // End of the recalculation.
            value = my_obj1->get_energy();  // Get object energy
            saved_energy += value;          // Add into the sum
        }                                   // End of loop over objects
        unchanged = true;                   // Value is correct mark as unchanged.
    }

    return saved_energy/2.0;                // All interactions are counted twice.
}

/**
 * Write the current configuration to a file in a format that can be used to
 * reinitialize a configuration with the file based constructor. See the class
 * description for the file format.
 *
 * @param dest  This is a file descriptor that should be open for writing.
 * @return      Should return exit status (currently always OK).
 *
 * @todo        Incorporate error handling and exit status return that is
 *              correct.
 */
int config::write(FILE *dest ){
    int     i;
    object  *this_obj;
                                            // Write header with bounding box
    fprintf(dest, "%9f2 %9f2 \n", x_size, y_size );
    fprintf(dest, "%d\n", obj_list.size() );// And then the number of objects
    for(i = 0; i< obj_list.size(); i++){    // For each object in configuration
        this_obj = obj_list.get(i);         // Get the object and
        this_obj->write(dest);              // Write it to the file
    }
    return EXIT_SUCCESS;                    // Return all well
}

/**
 * @return The number of objects found in the configuration.
 */
int config::n_objects(){
    return obj_list.size();                 // Get size of object list.
}

/**
 * Count the number of different types of object are found in the current
 * configuration. Actually it just returns the highest object type number found.
 *
 * @todo    Clean up semantics. What is actually needed and return this.
 * @return  as an integer the total number of different object types found.
 *
 */
int config::object_types(){
    int     i;
    int     max_type = -1;

    assert( check() );

    for(i = 0; i< obj_list.size(); i++ ){
        max_type = max(max_type, obj_list.get(i)->o_type);
    }
    assert(max_type>=0);
//  assert( check() );
    return max_type;
}

/**
 * Private member function to verify that the config structure is internally
 * valid.
 *
 * Internal validations (should) include:
 * - the object_list is a valid list.
 * - all objects in the list are valid.
 * - all objects are within the area of the configuration.
 * - if 'unchanged' is true then saved_energy is the energy obtained by
 *   calculation. This is hard to check as usually the force field is not
 *   available to the check function.
 *
 * On entry to and on exit from all of the functions in the config class
 *  this->check() should return 'true'.
 *
 * @return true or false depending on evaluation.
 *
 * @todo  Implement this function.
 */
bool config::check(){
    return true;
}

/**
 * This function compares atom by atom the current configuration and the
 * reference configuration and calculates the root mean square distance between
 * the atoms.
 *
 * @param ref   a reference configuration
 * @return      as a double the rms distance
 *
 * @todo        Currently just a stub, returns 1.0
 */
double config::rms(const config& ref){
    return 1.0;
}

/**
 * This function changes the size of a configuration by an isometric expansion
 * moving all the objects apart. It does not change the orientations of the
 * various objects.
 *
 * @param dl    The multiplicative factor to apply to the size and the object
 *              coordinates.
 * @return      No return value.
 */
void config::expand(double dl){
    int     i;

    x_size *= dl;                           // Expand boundary
    y_size *= dl;
    unchanged = false;                      // The energies will be different
    for(i=0;i<obj_list.size();i++){
        obj_list.get(i)->recalculate = true;// Also for the objects
        obj_list.get(i)->expand(dl);        // Move objects in rescaled box
    }
}

/**
 * Move a given object to a new place using the scaling factor dl_max
 * to control the distance distribution.
 *
 * @param obj_number the index of the object to move
 * @param dl_max the scaling parameter.
 */
void config::move(int obj_number, double dl_max){
    obj_list.get(obj_number)->move(dl_max, x_size, y_size, is_periodic );
    obj_list.get(obj_number)->rotate(M_2PI);// Mix for the moment move and rotate
}

/**
 * Rotate an object designated by the obj_number a random angle
 * scaled by theta_max.
 *
 * @param obj_number The index of the object to move
 * @param theta_max The scaling parameter.
 */
void config::rotate(int obj_number, double theta_max){
    obj_list.get(obj_number)->rotate(theta_max);
}

/**
 * Mark as needing recalculation of energies all objects within a certain
 * distance of a reference object.
 *
 * @param distance the cut-off distance to use.
 * @param index the number of the reference object.
 */
void    config::invalidate_within(double distance, int index){
    object  *obj1;
    object  *obj2;

    obj1 = obj_list.get(index);
    for(int i=0; i< n_objects(); i++)       // For each object in the cnfiguration
      if (i!= index){                       // That is difference
        obj2 = obj_list.get(i);             // Check distance
        if( obj1->distance(obj2, x_size, y_size, is_periodic) < distance )
            obj2->recalculate = true;       // and set flag if necessary
    }
}

/** \brief Associate a topology with the configuration
 *
 * \param a_topology a pointer to the topology.
 *
 */

void    config::add_topology(topology* a_topology){
    if( the_topology )                      // If there is already one
        delete( the_topology );             // Get rid of it
    the_topology = a_topology;              // Make the new association
}

/** \brief Insert an object into the configuration.
 *
 * \param orig the object to add
 * \return nothing
 *
 */
void    config::add_object(object* orig ){
    obj_list.add(orig);
}

/** \brief Output a postscript snippet to draw the configuration
 *
 * \param the_forces forcefield, needed for atom sizes and colors.
 * \param dest the file for the output.
 * \return no return value
 *
 * \todo use return value for error handling.
 */

void    config::ps_atoms(force_field* the_forces, FILE* dest){
    object  *my_obj;
    double  theta, dx, dy, r, x, y;
    int     t, lr, tb;
                                            // Loop over the objects.
    for(int i = 0; i < obj_list.size(); i++){
        my_obj = obj_list.get(i);
        theta  = my_obj->orientation;
                                            // Loop over the atoms
        for(int j = 0; j < the_topology->n_atom(my_obj->o_type); j++ ){
                                            // Get atom information
            t  =  the_topology->atoms(my_obj->o_type, j)->type;
            dx =  the_topology->atoms(my_obj->o_type, j)->x_pos;
            dy =  the_topology->atoms(my_obj->o_type, j)->y_pos;
            r  =  the_forces->size(t);// Get radius
                                            // Calculate atom position
            x  =  my_obj->pos_x + dx * cos(theta) - dy * sin(theta);
            y  =  my_obj->pos_y + dx * sin(theta) + dy * cos(theta);
                                            // Write postscript snippet for atom.
            fprintf(dest, "newpath %g %g %g %s moveto fcircle \n",
                    r, x, y, the_forces->get_color(t) );

                                            // Handle intersections with the border
            lr = 0; tb = 0;                 // need up to 4 copies.
            if ( x < r ) lr = -1;
            if ( x > x_size - r ) lr = +1;
            if ( y < r ) tb = -1;
            if ( y > y_size - r ) tb = +1;
            if (lr != 0 ){                  // Copy on other side
	            fprintf(dest, "newpath %g %g %g %s moveto fcircle \n",
                    r, lr*x-lr*x_size, y, the_forces->get_color(t) );
            }
            if (tb != 0 ){                  // Vertical copy
	            fprintf(dest, "newpath %g %g %g %s moveto fcircle \n",
                    r, x, tb*y-tb*y_size,
                    the_forces->get_color(t) );
            }
            if ((lr != 0 )&&(tb != 0)){     // In the corner!
	            fprintf(dest, "newpath %g %g %g %s moveto fcircle \n",
                    r, lr*x-lr*x_size, tb*y-tb*y_size,
                    the_forces->get_color(t) );
            }
        }
    }
}
